#include <iostream>
#include <sys/time.h>
#include <queue>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/epoll.h>
#include <cstring>
#include <sys/stat.h>
#include "epoll.h"
#include "util.h"
#include "requestData.h"

using namespace std;

pthread_mutex_t queueLock=PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t MimeType::lock=PTHREAD_MUTEX_INITIALIZER;
unordered_map<string,string> MimeType::mime;

priority_queue<mytimer*,deque<mytimer*>,timerCmp> mTimerQueue;

string MimeType::getMime(const string &keyword)
{
    if(mime.empty())
    {
        pthread_mutex_lock(&lock);
        if(mime.empty())
        {
            
            mime[".html"]="text/html";
            mime[".avi"] = "video/x-msvideo";
            mime[".bmp"] = "image/bmp";
            mime[".c"] = "text/plain";
            mime[".doc"] = "application/msword";
            mime[".gif"] = "image/gif";
            mime[".gz"] = "application/x-gzip";             
            mime[".htm"] = "text/html";
            mime[".ico"] = "application/x-ico";
            mime[".jpg"]="video/x-msvideo";
            mime[".png"]="image/png";
            mime[".txt"]="text/plain";
            mime[".mp3"]="audio/mp3";
            mime["default"]="text/html";
        }
        pthread_mutex_unlock(&lock);
    }
    if(mime.find(keyword)==mime.end())
        return mime["default"];
    else
        return mime[keyword];
}

mytimer::mytimer(requestData* _request,int timeout):deleted(false),request_data(_request)
{
    struct timeval tv;
    gettimeofday(&tv,NULL);
    expired_time=(tv.tv_sec*1000)+(tv.tv_usec/1000)+timeout;
}

mytimer::~mytimer()
{
    cout<<"~mytimer()"<<endl;
    if(request_data!=NULL)
    {
        cout<<"requst_data="<<request_data<<endl;
        delete request_data;
        request_data=NULL;
    }
}

void mytimer::update(int timeout)
{

    struct timeval tv;
    gettimeofday(&tv,NULL);
    expired_time=(tv.tv_sec*1000)+(tv.tv_usec/1000)+timeout;
}

bool mytimer::isValid()
{    
    struct timeval tv;
    gettimeofday(&tv,NULL);
    size_t now=(tv.tv_sec*1000)+(tv.tv_usec/1000);
    if(now<expired_time)
        return true;
    else{
        this->setDeleted();
        return false;
    }
}

void mytimer::clearRequest()
{
    request_data=NULL;
    this->setDeleted();
}

void mytimer::setDeleted()
{
    deleted=true;
}

bool mytimer::isDeleted()
{
    return deleted;
}

size_t mytimer::getExpTime() const
{
    return expired_time;
}

bool timerCmp::operator()(const mytimer *a,const mytimer *b)
{
    return a->getExpTime()>b->getExpTime();
}

requestData::requestData():againTimes(0),h_state(h_start),cur_read_pos(0)
    ,keepAlive(false),timer(NULL),state(STATE_PARSE_URI)
{
    cout<<"requestData have constructed!"<<endl;
}


requestData::requestData(int _epfd,int _lfd,string _path):againTimes(0),h_state(h_start),cur_read_pos(0)
    ,keepAlive(false),timer(NULL),state(STATE_PARSE_URI),epfd(_epfd),lfd(_lfd),path(_path)
{
    cout<<"requestData have constructed!"<<endl;
}

requestData::~requestData()
{
    cout<<"~requestData"<<endl;
    struct epoll_event ev;
    ev.events=EPOLLIN|EPOLLET|EPOLLONESHOT;
    ev.data.ptr=(void*)this;
    epoll_ctl(epfd,EPOLL_CTL_DEL,lfd,&ev);
    if(timer!=NULL)
    {
        timer->clearRequest();
        timer=NULL;
    }
    close(lfd);
}

void requestData::addTimer(mytimer *_timer)
{
    if(timer==NULL)
        timer=_timer;
}

void requestData::seperateTimer()
{
    if(timer!=NULL)
    {
        timer->clearRequest();
        timer=NULL;
    }
}

void requestData::reset()
{
    againTimes=0;
    content.clear();
    h_state=h_start;
    state=STATE_PARSE_URI;
    cur_read_pos=0;
    keepAlive=false;
    headers.clear();
    fileName.clear();
    path.clear();
}

int requestData::getFd()
{
    return lfd;
}

void requestData::setFd(int _lfd)
{
    lfd=_lfd;
}

void requestData::handleRequest()
{
    char buf[MAX_BUFF];
    bool isError=false;//若错误，需要删除这个数据
    while(true)
    {
        int read_num=readn(lfd,buf,MAX_BUFF);
        if(read_num<0)
        {
            perror("handleRequest readn error");
            isError=true;
            break;
        }
        else if(read_num==0)
        {
            //有要求但是读不到数据，可能是RequestAborted,或者来自网络的数据没有达到
            perror("handleRequest read_num==0");
            if(errno==EAGAIN)
            {
                if(againTimes>MAX_AGAIN_TIMES)
                    isError=true;
                else
                    againTimes++;
            }
            else if(errno!=0)
                isError=true;
            break;
        }

        string new_read(buf,buf+read_num);
        content+=new_read;

        if(state==STATE_PARSE_URI)
        {
            cout<<"STATE_PARSE_URI"<<endl;
            int flag=this->parseURI();
            if(flag==PARSE_URI_AGAIN)
                continue;
            else if(flag==PARSE_URI_ERROR)
            {
                perror("handleRequest PARSE_URI_ERROR");
                isError=true;
                break;
            }
        }
        if(state==STATE_PARSE_HEADERS)
        {
            cout<<"STATE_PARSE_HEADERS"<<endl;
            int flag=this->parseHeaders();
            if(flag==PARSE_HEADERS_AGAIN)
                continue;
            else if(flag==PARSE_HEADERS_ERROR)
            {
                perror("handleRequest PARSE_HEADERS_ERROR");
                isError=true;
                break;
            }
            if(method==METHOD_POST)
                state=STATE_RECV_BODY;
            else
                state=STATE_ANALYSIS;
        }
        if(state==STATE_RECV_BODY)
        {
            int content_length=-1;
            if(headers.find("Content-Length")!=headers.end())
                content_length=stoi(headers["Content-Length"]);
            else if(headers.find("Content-length")!=headers.end())
                content_length=stoi(headers["Content-length"]);
            else{
                isError=true;
                break;
            }
            if(content.size()<content_length)
                continue;
            state=STATE_ANALYSIS;
        }
        if(state==STATE_ANALYSIS)
        {
            int flag=this->analysisRequest();
            if(flag<0)
            {
                isError=true;
                break;
            }
            else if(flag==ANALYSIS_SUCCESS)
            {
                state=STATE_FINISH;
                break;
            }
            else
            {
                isError=true;
                break;
            }
        }

    }
    if(isError)
    {
        delete this;
        return;
    }

    if(state==STATE_FINISH)
    {
        if(keepAlive)
        {
            printf("ok\n");
            this->reset();
        }
        else{
            delete this;
            return;
        }
    }

    pthread_mutex_lock(&queueLock);
    mytimer *mtimer=new mytimer(this,500);
    timer=mtimer;
    mTimerQueue.push(mtimer);
    pthread_mutex_unlock(&queueLock);

    uint32_t _epoll_ev=EPOLLIN||EPOLLET||EPOLLONESHOT;
    int ret=epoll_mod(epfd,lfd,static_cast<void*>(this),_epoll_ev);
    if(ret<0)
    {
        delete this;
        return;
    }
}


void requestData::handleError(int fd,int err_num,string err_msg)
{
    err_msg=" "+err_msg;
    char send_buf[MAX_BUFF];
    string body_buf,header_buf;
    body_buf+="<html><title>TKeed Error</title>";
    body_buf+="<body bgcolor=\"ffffff\">";
    body_buf+=to_string((long long int)err_num);
    body_buf+=err_msg;
    body_buf+="<hr><em> TamCarmen's Web Server</em>\n</body></html>";

    header_buf+="HTTP/1.1 "+to_string((long long int)err_num)+err_msg+"\r\n";
    header_buf+="Content-type:text/html\r\n";
    header_buf+="Connection:close\r\n";
    header_buf+="Content-length:"+to_string((long long unsigned int)body_buf.size())+"\r\n";
    header_buf+="\r\n";

    sprintf(send_buf,"%s",header_buf.c_str());
    writen(fd,send_buf,sizeof(send_buf));
    sprintf(send_buf,"%s",body_buf.c_str());
    writen(fd,send_buf,sizeof(send_buf));
}

int requestData::parseURI()
{
    string &ctx=content;
    int pos=ctx.find('\n',cur_read_pos);
    if(pos<0)
    {
        cout<<"parseURI pos<0"<<endl;
        return PARSE_URI_AGAIN;
    }

    string request_line=ctx.substr(0,pos);
    if(ctx.size()>pos+1)//切割
        ctx=ctx.substr(pos+1);
    else
        ctx.clear();

    pos=request_line.find("GET");
    if(pos<0)
    {
        pos=request_line.find("POST");
        if(pos<0)
            return PARSE_URI_ERROR;
        else
            method=METHOD_POST;
    }
    else
    {
        method=METHOD_GET;
    }

    pos=request_line.find('/',pos);
    if(pos<0)
        return PARSE_URI_ERROR;
    else
    {
        int _pos=request_line.find(' ',pos);
        if(_pos<0)
            return PARSE_URI_ERROR;
        else
        {
            if(_pos-pos>1)
            {
                fileName=request_line.substr(pos+1,_pos-pos-1);
                int __pos=fileName.find('?');
                if(__pos>=0)
                {
                    fileName=fileName.substr(0,__pos);
                }
            }
            else 
                fileName="index.html";
        }
        pos=_pos;
    }
    //http 版本号
    pos=request_line.find("/",pos);
    if(pos<0)
    {
        return PARSE_URI_ERROR;
    }
    else
    {
        if(request_line.size()-pos<=3)
        {
            return PARSE_URI_ERROR;
        }
        else
        {
            string ver=request_line.substr(pos+1,3);
            if(ver=="1.0")
                HTTPversion=HTTP_10;
            else if(ver=="1.1")
                HTTPversion=HTTP_11;
            else
                return PARSE_URI_ERROR;
         }
     }
    state=STATE_PARSE_HEADERS;
    cout<<"method:"<<method<<" fileName:"<<fileName<<" HTTP version:"<<HTTPversion<<endl;
    return PARSE_URI_SUCCESS;
}

int requestData::parseHeaders()
{
    string &ctx=content;
    int key_start=-1,key_end=-1,value_start=-1,value_end=-1;
    int now_read_line_begin=0;
    bool notFinish=true;
    
    for(int i=0;i<ctx.size()&&notFinish;i++)
    {
        cout<<"进入循环"<<endl;
        switch(h_state)
        {
            case h_start:
                cout<<"换行"<<endl;
                if(ctx[i]=='\n'||ctx[i]=='\r')
                    break;
                cout<<"h_start"<<endl;
                h_state=h_key;
                key_start=i;
                now_read_line_begin=i;
                break;
            case h_key:
                cout<<"h_key"<<endl;
                if(ctx[i]==':')
                {
                    cout<<"h_key finish"<<endl;
                    key_end=i;
                    if(key_end-key_start<=0)
                        return PARSE_HEADERS_ERROR;
                    h_state=h_colon;
                }
                else if(ctx[i]=='\n'||ctx[i]=='\r')
                    return PARSE_HEADERS_ERROR;
                break;
            case h_colon:
                if(ctx[i]==' ')
                {
                    h_state=h_spaces_after_colon;
                }
                else 
                    return PARSE_HEADERS_ERROR;
                break;
            case h_spaces_after_colon:
                h_state=h_value;
                value_start=i;
                break;
            case h_value:
                if(ctx[i]=='\r')
                {
                    h_state=h_CR;
                    value_end=i;
                    if(value_end-value_start<=0)
                        return PARSE_HEADERS_ERROR;
                }
                else if(i-value_start>255)
                    return PARSE_HEADERS_ERROR;
                break;
            case h_CR:
                if(ctx[i]=='\n')
                {
                    h_state=h_LF;
                    string key(ctx.begin()+key_start,ctx.end()+key_end);
                    string value(ctx.begin()+value_start,ctx.end()+value_end);
                    headers[key]=value;
                    now_read_line_begin=i;
                }
                else
                    return PARSE_HEADERS_ERROR;
                break;
            case h_LF:
                if(ctx[i]=='\r')
                {
                    h_state=h_end_CR;
                }
                else
                {
                    key_start=i;
                    h_state=h_key;
                }
                break;
            case h_end_CR:
                if(ctx[i]=='\n')
                {
                    h_state=h_end_LF;
                }
                else
                    return PARSE_HEADERS_ERROR;
                break;
            case h_end_LF:
                notFinish=false;
                key_start=i;
                now_read_line_begin=i;
                break;
        }    
    }
    if(h_state=h_end_LF)
    {
        ctx=ctx.substr(now_read_line_begin);
        return PARSE_HEADERS_SUCCESS;
    }
    ctx=ctx.substr(now_read_line_begin);
    cout<<"PARSE_HEADERS_AGAIN"<<endl;
    return PARSE_HEADERS_AGAIN;
}

int requestData::analysisRequest()
{
    if(method==METHOD_POST)
    {
        char header[MAX_BUFF];
        int ver=0;
        if(HTTPversion==HTTP_10)
            ver=0;
        else if(HTTPversion==HTTP_11)
            ver=1;
        sprintf(header,"HTTP/1.%d %d %s\r\n",ver,200,"OK");

        if(headers.find("Connection")!=headers.end()&&headers["Connection"]=="keep-alive")
        {
            keepAlive=true;
            sprintf(header,"%sConnection: keep-alive\r\n",header);
            sprintf(header,"%sKeep-Alive: timeout=%d\r\n",header,EPOLL_WAIT_TIME);
        }   

        char send_content[]="I have receiced this.";

        sprintf(header,"%sContent-length: %zu\r\n",header,strlen(send_content));
        sprintf(header,"%s\r\n",header);
        size_t send_len=(size_t)writen(lfd,header,strlen(header));
        if(send_len!=strlen(header))
        {
            perror("Send header failed");
            return ANALYSIS_ERROR;
        }

        send_len=(size_t)writen(lfd,send_content,strlen(send_content));
        if(send_len!=strlen(send_content))
        {
            perror("Send content failed");
            return ANALYSIS_ERROR;
        }

        cout<<"content size =="<<content.size()<<endl;
        
        /*
         *对content中的内容，如图片，编码进入内存缓冲区
         * */

        return ANALYSIS_SUCCESS;
    }
    else if(method==METHOD_GET)
    {   
        char header[MAX_BUFF];
        int ver=0;
        if(HTTPversion==HTTP_10)
            ver=0;
        else if(HTTPversion==HTTP_11)
            ver=1;
        sprintf(header,"HTTP/1.%d %d %s\r\n",ver,200,"OK");

        if(headers.find("Connection")!=headers.end()&&headers["Connection"]=="keep-alive")
        {
            keepAlive=true;
            sprintf(header,"%sConnection: keep-alive\r\n",header);
            sprintf(header,"%sKeep-Alive: timeout=%d\r\n",header,EPOLL_WAIT_TIME);
        }

        int dot_pos=fileName.find('.');
        const char* file_type;
        if(dot_pos<0)
            file_type=MimeType::getMime("default").c_str();
        else
            file_type=MimeType::getMime(fileName.substr(dot_pos)).c_str();

        struct stat sbuf;

        if(stat(fileName.c_str(),&sbuf)<0)
        {
            handleError(lfd,404,"Not Found!");
            return ANALYSIS_ERROR;
        }

        sprintf(header,"%sContent-type: %s\r\n",header,file_type);
        sprintf(header,"%sContent-length: %ld\r\n",header,sbuf.st_size);
        sprintf(header,"%\r\n",header);

        size_t send_len=(size_t)writen(lfd,header,strlen(header));
        if(send_len!=strlen(header))
        {
            perror("Send header failed");
            return ANALYSIS_ERROR;
        }
        
        int src_fd=open(fileName.c_str(),O_RDONLY,0);
        char *src_addr=static_cast<char *>(mmap(NULL,sbuf.st_size,PROT_READ,MAP_PRIVATE,src_fd,0));
        close(src_fd);

        send_len=writen(lfd,src_addr,sbuf.st_size);
        if(send_len!=sbuf.st_size)
        {
            perror("Send file failed");
            return ANALYSIS_ERROR;
        }

        munmap(src_addr,sbuf.st_size);
        return ANALYSIS_SUCCESS;

    }
    else
        return ANALYSIS_ERROR;
}



