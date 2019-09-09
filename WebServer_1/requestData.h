#ifndef REQUESTDATA
#define REQUESTDATA

#include <string>
#include <unordered_map>

const int STATE_PARSE_URI=1;
const int STATE_PARSE_HEADERS=2;
const int STATE_RECV_BODY=3;
const int STATE_ANALYSIS=4;
const int STATE_FINISH=5;

const int PARSE_URI_AGAIN=-1;
const int PARSE_URI_ERROR=-2;
const int PARSE_URI_SUCCESS=0;

const int PARSE_HEADERS_AGAIN=-1;
const int PARSE_HEADERS_ERROR=-2;
const int PARSE_HEADERS_SUCCESS=0;

const int ANALYSIS_ERROR=-2;
const int ANALYSIS_SUCCESS=0;

const int METHOD_POST=1;
const int METHOD_GET=2;

const int HTTP_10=1;
const int HTTP_11=2;

const int MAX_AGAIN_TIMES=200;

const int MAX_BUFF=4096;

const int EPOLL_WAIT_TIME=500;

enum HeadersState
{
    h_start=0,
    h_key,
    h_colon,
    h_spaces_after_colon,
    h_value,
    h_CR,
    h_LF,
    h_end_CR,
    h_end_LF
};

class MimeType
{
private:
    static pthread_mutex_t lock;
    static std::unordered_map<std::string,std::string> mime;
    MimeType();
    MimeType(const MimeType& m);
public:
    static std::string getMime(const std::string& keyword);
};

struct requestData;
struct mytimer;

struct mytimer
{
    bool deleted;
    size_t expired_time;
    requestData *request_data;

    mytimer(requestData* _request,int timeout);
    ~mytimer();
    void update(int timeout);
    bool isValid();
    void clearRequest();
    void setDeleted();
    bool isDeleted();
    size_t getExpTime() const;
};

struct timerCmp
{
    bool operator()(const mytimer *a,const mytimer *b);
};

struct requestData
{
private:
    int againTimes;//重试次数
    std::string path;//文件路径
    int lfd;//监听文件描述符
    int epfd;//epoll文件描述符
    std::string content;//当前内容，随着read和write更新
    int method;//方法类型，post或者get
    int HTTPversion;//HTTP版本1.0或1.1
    std::string fileName;//请求报文要访问的资源名字
    int h_state;//请求头部的分析状态
    int state;//请求报文的分析状态
    int cur_read_pos;//当前阅读的位置
    bool isFinsh;//分析是否结束
    bool keepAlive;//判断链接是否继续
    std::unordered_map<std::string,std::string> headers;//头部的关键字与值对
    mytimer *timer;//定时器 
public:
    requestData();
    requestData(int _epfd,int _lfd,std::string _path);
    ~requestData();
    void addTimer(mytimer* _timer);
    void seperateTimer();
    void reset();
    int getFd();
    void setFd(int _lfd);
    void handleRequest();
    void handleError(int fd,int err_num,std::string err_msg);
private:
    int parseURI();
    int parseHeaders();
    int analysisRequest();
};

#endif
