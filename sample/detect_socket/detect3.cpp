#include <iostream>
#include <queue>
#include "detect_t.h"
#include <time.h>
#include "KVConfig.h"
#include "sample_comm.h"
#include "hi_comm_ive.h"
#include "hi_ive.h"
#include "mpi_ive.h"
#include "opencv2/imgproc/imgproc_c.h"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/video/video.hpp"
#include "cJSON.h"
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <cstdio>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include "libdetect_t.h"
#include "base64.h"
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <vector>
#include "detect_main.h"
using namespace std;

const char *version = "v0.06";

const VI_CHN ExtChn = VIU_EXT_CHN_START;
KVConfig *cfg_ = NULL;
KVConfig *cfg_stu = NULL;
KVConfig *cfg_db = NULL;
TeacherDetecting *pdet_teacher = NULL;
det_t *pdet_db = NULL;
struct Ctx *pdet_stu = NULL;
int getframe_init(int width, int height, int ExtChn);
int getframe(Mat *Img, int ExtChn);
void vector_to_json_t(std::vector < Rect > r, cv::Rect upbody_rect, bool is_upbody, bool is_rect, char *buf);
void AnalyzePic();
void RecvCMD();
void sendPoi();
void sig_pipe(int signo);
void upgrade(int size);
int return_ok(int connectfd);
int return_failed(int connectfd);
int socketconnect(int socketfd);
int setParam(cJSON *json);
void sendVersion(int connectfd);
void sendConf(int connectfd, int blackboard_conf);
void sendModelandStatus(int connectfd);
void recvResultfromSlave();
void recvResultfromSlaveCleanup(void *arg);
cv::Point2f* getCorrPoint(const char *str, cv::Point2f *p);
cv::Point getPoint(const char* message);
cv::Point GetTransformPoint(cv::Point src, cv::Mat warp);


int connfd;
int socketfd;
int disconnect = 1;
queue<cv::Point> master_queue;
queue<cv::Point> slave_queue;
cv::Mat warp_mat;
//int g_teacher_cfg_changed = 0;
//int g_stu_cfg_changed = 0;
//int g_bd_cfg_changed = 0;

int g_reset = 0;

int start_analysis = 0;

int main(void)
{
    pthread_t pictid, recvcmdtid;
    getframe_init(480, 270, VIU_EXT_CHN_START);
    cfg_ = new KVConfig("/home/teacher_detect_trace.config");
    signal(SIGPIPE, sig_pipe);
    pthread_create(&pictid, NULL, (void*(*)(void*))AnalyzePic, NULL);
    pthread_create(&recvcmdtid, NULL, (void*(*)(void*))RecvCMD, NULL);
    while (1)
    {
        sleep(10);
    }
    return 0;
}

int getframe_init(int width, int height, int ExtChn)
{
    VI_CHN Vichn = 0;
    VI_EXT_CHN_ATTR_S stExtChnAttr;
    HI_S32 s32Ret;

    stExtChnAttr.enPixFormat = PIXEL_FORMAT_YUV_SEMIPLANAR_420;
    stExtChnAttr.s32BindChn = Vichn;
    stExtChnAttr.stDestSize.u32Width = width;
    stExtChnAttr.stDestSize.u32Height = height;
    stExtChnAttr.s32DstFrameRate = -1;
    stExtChnAttr.s32SrcFrameRate = -1;
    stExtChnAttr.enCompressMode = COMPRESS_MODE_NONE;

    HI_MPI_VI_DisableChn(ExtChn);

    s32Ret = HI_MPI_VI_SetExtChnAttr(ExtChn, &stExtChnAttr);
    if (HI_SUCCESS != s32Ret)
    {
        printf("HI_MPI_VI_SetExtChnAttr failed with err code %#x\n", s32Ret);
        return -1;
    }

    s32Ret = HI_MPI_VI_SetFrameDepth(ExtChn, 1);
    if (HI_SUCCESS != s32Ret)
    {
        printf("HI_MPI_VI_SetFrameDepth failed with err code %#x\n", s32Ret);
        return -1;
    }

    s32Ret = HI_MPI_VI_EnableChn(ExtChn);
    if (HI_SUCCESS != s32Ret)
    {
        printf("HI_MPI_VI_EnableChn failed with err code %#x\n", s32Ret);
        return -1;
    }
    return 1;
}

int getframe(Mat *Img, int ExtChn)
{
    HI_S32 s32Ret;
    HI_U32 u32BlkSize, u32DstBlkSize;
    VIDEO_FRAME_INFO_S FrameInfo;
    IVE_SRC_IMAGE_S stSrc;
    IVE_DST_IMAGE_S stDst;
    IVE_CSC_CTRL_S stCscCtrl;
    HI_BOOL bInstant = HI_TRUE;
    HI_BOOL bFinish;
    IVE_HANDLE IveHandle;


    stSrc.enType = IVE_IMAGE_TYPE_YUV420SP;
    stDst.enType = IVE_IMAGE_TYPE_U8C3_PACKAGE;
    stCscCtrl.enMode = IVE_CSC_MODE_PIC_BT709_YUV2RGB;

    s32Ret = HI_MPI_VI_GetFrame(ExtChn, &FrameInfo, -1);
    if (HI_SUCCESS != s32Ret)
    {
        printf("HI_MPI_VI_GetFrame failed with err code %#x!\n", s32Ret);
        return -1;
    }

    u32DstBlkSize = FrameInfo.stVFrame.u32Stride[0] * FrameInfo.stVFrame.u32Height * 3;
    s32Ret = HI_MPI_SYS_MmzAlloc_Cached(&stDst.u32PhyAddr[0], (void**)&stDst.pu8VirAddr[0], "user", HI_NULL, u32DstBlkSize);
    if (s32Ret != HI_SUCCESS)
    {
        printf("HI_MPI_SYS_MmzAlloc_Cached failed with err code %#x!\n", s32Ret);
    }
    HI_MPI_SYS_MmzFlushCache(stDst.u32PhyAddr[0], (void**)stDst.pu8VirAddr[0], u32DstBlkSize);

    stDst.pu8VirAddr[0] = (HI_U8*) HI_MPI_SYS_Mmap(stDst.u32PhyAddr[0], u32DstBlkSize);
    stDst.u16Stride[0] = FrameInfo.stVFrame.u32Stride[0];
    stDst.u16Width = FrameInfo.stVFrame.u32Width;
    stDst.u16Height = FrameInfo.stVFrame.u32Height;

    stSrc.u32PhyAddr[0] = FrameInfo.stVFrame.u32PhyAddr[0];
    stSrc.u32PhyAddr[1] = FrameInfo.stVFrame.u32PhyAddr[1];
    stSrc.u32PhyAddr[2] = FrameInfo.stVFrame.u32PhyAddr[2];

    stSrc.pu8VirAddr[0] = (HI_U8*)HI_MPI_SYS_Mmap(FrameInfo.stVFrame.u32PhyAddr[0], u32DstBlkSize / 2);
    stSrc.pu8VirAddr[1] = stSrc.pu8VirAddr[0] + FrameInfo.stVFrame.u32Stride[0] * FrameInfo.stVFrame.u32Height;
    stSrc.pu8VirAddr[2] = stSrc.pu8VirAddr[1] + 1;


    stSrc.u16Stride[1] = FrameInfo.stVFrame.u32Stride[0];
    stSrc.u16Stride[0] = FrameInfo.stVFrame.u32Stride[0];
    stSrc.u16Stride[2] = 0;

    stSrc.u16Width = FrameInfo.stVFrame.u32Width;
    stSrc.u16Height = FrameInfo.stVFrame.u32Height;

    s32Ret = HI_MPI_IVE_CSC(&IveHandle, &stSrc, &stDst, &stCscCtrl, bInstant);
    if (s32Ret != HI_SUCCESS)
    {
        printf("HI_MPI_IVE_CSC failed with error code %#x\n", s32Ret);
    }

    s32Ret = HI_MPI_IVE_Query(IveHandle, &bFinish, HI_TRUE);
    if (s32Ret != HI_SUCCESS)
    {
        printf("HI_MPI_IVE_Query failed with error code %#x\n", s32Ret);
    }

    memcpy((void*)(Img->data), (void*)stDst.pu8VirAddr[0], u32DstBlkSize);

    HI_MPI_SYS_Munmap(stDst.pu8VirAddr[0], u32DstBlkSize);
    HI_MPI_SYS_Munmap(stSrc.pu8VirAddr[0], u32DstBlkSize / 2);
    HI_MPI_SYS_MmzFree(stDst.u32PhyAddr[0], stDst.pu8VirAddr[0]);
    HI_MPI_VI_ReleaseFrame(ExtChn, &FrameInfo);
}

void vector_to_json_t(std::vector < Rect > r, cv::Rect upbody_rect, bool is_upbody, bool is_rect, char *buf)
{
    int offset = 0;
    offset = sprintf(buf, "{\"stamp\":%d,", time(0));

    if (!is_rect)
    {
        offset += sprintf(buf + offset, "\"rect\":[ ],");
    }
    else
    {
        offset += sprintf(buf + offset, "\"rect\":[");

        for (int i = 0; i < r.size(); i++)
        {
            Rect t = r[i];
            if (i == 0)
                offset +=
                    sprintf(buf + offset,
                            "{\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d}",
                            t.x, t.y, t.width, t.height);
            else
                offset +=
                    sprintf(buf + offset,
                            ",{\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d}",
                            t.x, t.y, t.width, t.height);
        }

        offset += sprintf(buf + offset, "],");
    }

    if (!is_upbody)
    {
        offset += sprintf(buf + offset, "\"up_rect\":{\"x\":0,\"y\":0,\"width\":0,\"height\":0}");
    }
    else
    {
        offset += sprintf(buf + offset, "\"up_rect\":");
        offset += sprintf(buf + offset, "{\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d}",
                          upbody_rect.x, upbody_rect.y, upbody_rect.width, upbody_rect.height);
    }

    strcat(buf, "}");
}

void AnalyzePic()
{
    bool isrect;
    vector<cv::Rect> first_r;
    vector<cv::Rect> r;
    while (1) {
        cfg_->reload();

        if (pdet_teacher != NULL)
        {
            delete pdet_teacher;
            printf("delete detect\n");
            pdet_teacher = NULL;
        }
        if (pdet_db != NULL)
        {
            det_close(pdet_db);
            printf("det_close\n");
            pdet_db = NULL;
            cfg_db = NULL;
        }
        if (pdet_stu != NULL)
        {
            det_stu_close(pdet_stu);
            printf("det_close\n");
            pdet_stu = NULL;
            cfg_stu = NULL;
        }
        printf("start\n");
        int model = atoi(cfg_->get_value("model"));
        printf("%d\n",model);
        if (model == 0)
        {
            printf("teacher detect!\n");
            pdet_teacher = new TeacherDetecting(cfg_);
            //g_teacher_cfg_changed = 0;
            g_reset = 0;

            while (1)
            {
                /*
                if (g_teacher_cfg_changed == 1)
                {
                    break;
                }
                */

                if(g_reset == 1)
                {
                    break;
                }

                if (start_analysis == 0)
                {
                    usleep(100);
                    continue;
                }
                char str[1024] = {};
                Mat Img(270, 480, CV_8UC3, 0);
                Img.create(270, 480, CV_8UC3);
                getframe(&Img, VIU_EXT_CHN_START);

                Mat masked_img_temp = Mat(Img, pdet_teacher->masked_rect);
                Mat masked_img;
                masked_img_temp.copyTo(masked_img);
                pdet_teacher->do_mask(masked_img);
                isrect = pdet_teacher->one_frame_luv(Img, masked_img, r, first_r);

                for (int i = 0; i < r.size(); i++)
                {
                    cv::Rect box = pdet_teacher->masked_rect;
                    r[i].x = r[i].x + box.x;
                    r[i].y = r[i].y + box.y;
                    r[i] &= cv::Rect(0, 0, Img.cols, Img.rows);
                }

                cv::Rect upbody;
                bool is_up_body = false;
                if (atoi(cfg_->get_value("t_upbody_detect", "0")) > 0)
                {
                    //printf("upbody\n");
                    Mat masked_upbody = Mat(Img, pdet_teacher->upbody_masked_rect);
                    //print_time("upbody");
                    pdet_teacher->get_upbody(masked_upbody);
                    //print_time("upbody1");
                    if (pdet_teacher->up_update.upbody_rect.size() > 0)
                    {
                        cv::Rect upbody_t = pdet_teacher->up_update.upbody_rect[0];
                        if (!(upbody_t.x + upbody_t.width / 2 > pdet_teacher->upbody_masked_rect.width - 15 || upbody_t.x + upbody_t.width / 2 < 15))
                        {
                            is_up_body = true;
                            cv::Rect box = pdet_teacher->upbody_masked_rect;
                            upbody = upbody_t;
                            upbody.x = upbody.x + box.x;
                            upbody.y = upbody.y + box.y;
                            upbody &= cv::Rect(0, 0, Img.cols, Img.rows);
                        }
                    }
                    //print_time("upbody2");
                }
                vector_to_json_t(r, upbody, is_up_body, isrect, str);
                //printf("%s\n",str);
                //if(socketconnect(socketfd) == 1)
                int ret = send(connfd, str, strlen(str) + 1, 0);
                if (ret == -1)
                {
                    start_analysis = 0;
                }
            }
        }
        else if(model == 1)
        {
            printf("blackboard detect!\n");
            pdet_db = det_open("bd_detect_trace.config");
            //g_bd_cfg_changed = 0;
            g_reset = 0;
            cfg_db = pdet_db->cfg_;
            while (1)
            {
                /*
                if (g_bd_cfg_changed == 1)
                {
                    break;
                }
                */

                if(g_reset == 1)
                {
                    break;
                }

                if (start_analysis == 0)
                {
                    usleep(100);
                    continue;
                }

                Mat Img(270, 480, CV_8UC3, 0);
                Img.create(270, 480, CV_8UC3);
                getframe(&Img, VIU_EXT_CHN_START);

                char *str = det_detect(pdet_db, 270, 480, Img.data);
                int ret = send(connfd, str, strlen(str) + 1, 0);
                if (ret == -1)
                {
                    start_analysis = 0;
                }
            }
        }
        else if(model == 2)
        {
            printf("student detect!\n");
            pdet_stu = (struct Ctx*)det_stu_open("student_detect_trace.config");
            cfg_stu = pdet_stu->cfg_;
            //g_stu_cfg_changed = 0;
            g_reset = 0;
            Point2f  srcTri[4], dstTri[4];
            //read srcTri, dstTri from "student_detect_trace.conf"
            const char *srcStr = pdet_stu->cfg_->get_value("local_corr_point", 0);
            const char *dstStr = pdet_stu->cfg_->get_value("external_corr_point", 0);
            getCorrPoint(srcStr, srcTri);
            getCorrPoint(dstStr, dstTri);
            warp_mat = getPerspectiveTransform(srcTri, dstTri);
            while (1)
            {
                /*
                if (g_stu_cfg_changed == 1)
                {
                    break;
                }
                */

                if(g_reset == 1)
                {
                    break;
                }

                if (start_analysis == 0)
                {
                    usleep(100);
                    continue;
                }

                Mat Img(270, 480, CV_8UC3, 0);
                Img.create(270, 480, CV_8UC3);
                getframe(&Img, VIU_EXT_CHN_START);
                const char *str = det_detect(pdet_stu, Img);

                Point p = getPoint(str);
                //Point p;
                if(master_queue.size() >= 5)
                {
                    slave_queue.pop();
                }
                master_queue.push(p);
                //int ret = send(connfd, str, strlen(str) + 1, 0);
                //if (ret == -1)
                //{
                //  start_analysis = 0;
                //}
            }
        }
        else if (model == 3)
        {
            printf("student slave detect!\n");
            int slave_sockfd;
            //char *masterip = "10.1.2.2";
            struct sockaddr_in address;

            const char *masterip = pdet_stu->cfg_->get_value("student_master_ip", 0);

            memset(&address, 0, sizeof(struct sockaddr_in));
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = inet_addr(masterip);
            address.sin_port = htons(9001);

            slave_sockfd = socket(AF_INET, SOCK_DGRAM, 0);

            pdet_stu = (struct Ctx*)det_stu_open("student_detect_trace.config");
            cfg_stu = pdet_stu->cfg_;
            g_reset = 0;

            while (1)
            {
                /*
                if (g_stu_cfg_changed == 1)
                {
                    break;
                }
                */

                if(g_reset == 1)
                {
                    close(slave_sockfd);
                    break;
                }

                if (start_analysis == 0)
                {
                    usleep(100);
                    continue;
                }

                Mat Img(270, 480, CV_8UC3, 0);
                Img.create(270, 480, CV_8UC3);
                getframe(&Img, VIU_EXT_CHN_START);
                const char *str = det_detect(pdet_stu, Img);
                //int ret = send(connfd, str, strlen(str) + 1, 0);
                int ret = sendto(slave_sockfd, str, strlen(str) + 1, 0, (struct sockaddr *)&address, sizeof(address));
                if (ret == -1)
                {
                    start_analysis = 0;
                }
            }
        }
    }
}

void AnalyzeCMD(char *text)
{
    cJSON *json, *json_cmd, *json_filesize, *json_bd;
    json = cJSON_Parse(text);
    if (!json)
    {
        printf("Error before: [%s]\n", cJSON_GetErrorPtr());
        return_failed(connfd);
    }
    else
    {
        json_cmd = cJSON_GetObjectItem(json, "cmd");
        if (json_cmd->type == cJSON_Number)
        {
            switch (json_cmd->valueint)
            {
            case 1:
                if (setParam(json) == 1)
                {
                    return_ok(connfd);
                }
                else
                {
                    return_failed(connfd);
                }
                break;
            case 2:
                sendPoi();
                break;
            case 3:
                json_filesize = cJSON_GetObjectItem(json, "size");
                return_ok(connfd);
                if (json_filesize != NULL)
                {
                    upgrade(json_filesize->valueint);
                }
                else
                    return_failed(connfd);
                break;
            case 4:
                json_bd = cJSON_GetObjectItem(json, "model");
                if(json_bd == NULL)
                {
                    return_failed(connfd);
                    return;
                }
                //printf("model:%d\n",json_bd->valueint);
                cfg_->set_value("model",json_bd->valueint);
                cfg_->save_as("/home/teacher_detect_trace.config");
                //g_teacher_cfg_changed = 1;
                g_reset = 1;
                return_ok(connfd);
                break;
            case 5:
                g_reset = 1;
                start_analysis = 0;
                return_ok(connfd);
                break;
            case 6:
                system("reboot");
                break;
            case 100:
                //printf("version\n");
                sendVersion(connfd);
                break;
            case 101:
                sendConf(connfd, cJSON_GetObjectItem(json, "model")->valueint);
                break;
            case 102:
                sendModelandStatus(connfd);
            default:
                break;
            }
        }
        cJSON_Delete(json);
    }

}


void RecvCMD()
{
    struct sockaddr_in servaddr;
    char text[1024];
    int n;
    int ret;
    //struct timeval timeout = {10, 0};
    int keepAlive = 1;
    int reuse = 1;

    if ((socketfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("socket error ");
        exit(0);
    }
    //struct timeval timeout = {1,0};
    //setsockopt(socketfd, SOL_SOCKET, SO_RCVTIMEO, (void*)&timeout, sizeof(struct timeval));
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(9001);
    if (bind(socketfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) == -1)
    {
        perror("bind error");
        exit(0);
    }
    if (listen(socketfd, 10) == -1)
    {
        perror("listen error");
        exit(0);
    }
    while (1)
    {
        connfd = accept(socketfd, (struct sockaddr*)NULL, NULL);
        if (connfd == -1)
        {
            perror("accept error");
        }
        disconnect = 0;
        while (1)
        {
            if (disconnect == 1)
            {
                close(connfd);
                break;
            }
            usleep(20);
            n = recv(connfd, text, 1024, 0);
            if (n > 0)
            {
                AnalyzeCMD(text);
            }
            else
            {
                if (0 == socketconnect(socketfd))
                {
                    start_analysis = 0;
                    disconnect = 1;
                }
            }
        }
    }
}

void sendPoi()
{
    start_analysis = 1;
}
void upgrade(int size)
{
    cJSON *json, *json_upgrade;
    char recvstr[1024];
    char *text = (char*)malloc(2 * size * sizeof(char) + 50000);
    memset(text, 0, 2 * size * sizeof(char) + 50000);
    char *text_temp  = text;
    int sum = 0;
    int i = 0, j = 0;
    fd_set set;
    int first_ = 1;
    //printf("size:%d\n",size);
    while(1)
    {
        FD_ZERO(&set);
        FD_SET(connfd, &set);
        struct timeval timeout = {1, 0};
        select(connfd + 1, &set, NULL, NULL, &timeout);
        if(FD_ISSET(connfd, &set))
        {
            int n = recv(connfd, recvstr, sizeof(recvstr), 0);
            if(n < 0)
            {
                perror("recv");
                return;
            }
            memcpy(text_temp, recvstr, n);
            text_temp += n;
            sum += n;
            if(first_ == 1)
                first_ = 0;
        }
        else if(!FD_ISSET(connfd, &set) && first_ == 0)
        {
            break;
        }
        else
        {
            continue;
        }
        //printf("n = %d,sum = %d\n",n,sum);

    }
    printf("recv\n");
    json = cJSON_Parse(text);
    if (!json)
    {
        printf("Error before: [%s]\n", cJSON_GetErrorPtr());
        return_failed(connfd);
        return ;
    }
    json_upgrade = cJSON_GetObjectItem(json, "upgrade");
    unsigned char *bindata = (unsigned char*)malloc(2 * size * sizeof(char));
    char *strdecode = (char*)malloc(strlen(json_upgrade->valuestring) * sizeof(char) + 1);
    memset(strdecode, 0, strlen(json_upgrade->valuestring) + 1);
    while(1)
    {
        strdecode[j] = json_upgrade->valuestring[i];
        if(json_upgrade->valuestring[i] == '\0')
            break;
        if(strdecode[j] == '\n' || strdecode[j] == '\r')
            j--;
        j++;
        i++;
    }
    //int num = base64_decode(json_upgrade->valuestring, bindata);
    int num = base64_decode(strdecode, bindata);
    printf("num = %d,size = %d\n", num, size);
    if(num == size)
    {
        system("rm /home/detect3");
        FILE *fp = fopen("/home/detect3","w");
        fwrite(bindata, num, 1, fp);
        fclose(fp);
        system("chmod +x /home/detect3");
        return_ok(connfd);
        printf("ok\n");
        system("reboot");
    }
    else
    {
        //FILE *fp2 = fopen("/app/detect.txt","w");
        //fwrite(json_upgrade->valuestring, strlen(json_upgrade->valuestring), 1, fp2);
        free(text);
        free(bindata);
        return_failed(connfd);
    }
}
/*
void upgrade(int size)
{
    start_analysis = 0;
    char buf[1024] = {};
    int n = 0;
    int sum = 0;
    int ret;
    FILE *save = fopen("/home/detect_upgrade", "w+");
    if (save == NULL)
    {
        perror("fopen error");
        return ;
    }
    while (1)
    {
        n = recv(connfd, buf, sizeof(buf), 0);
        if (n < 0)
            perror("recv error");
        if (n == 0)
            break;
        fwrite(buf, n, 1, save);
        sum  += n;
    }
    fclose(save);
    if (sum == size)
    {
        system("chmod +x /home/detect_upgrade");
        system("mv  /home/detect_upgrade /home/detect3");
        ret = return_ok(connfd);
        if (ret < 0)
            perror("send error");
        printf("success !\n");
        system("reboot");
    }
    else
    {
        system("rm /home/detect_upgrade");
        ret = return_failed(connfd);
        if (ret < 0)
            perror("send error");
        printf("failed!\n");
    }
    disconnect = 1;
}
*/
int return_ok(int connectfd)
{
    cJSON *sendjson = cJSON_CreateObject();
    cJSON_AddStringToObject(sendjson, "result", "ok");
    char *sendstr = cJSON_Print(sendjson);
    int ret = send(connectfd, sendstr, strlen(sendstr) + 1, 0);
    cJSON_Delete(sendjson);
    return ret;
}

int return_failed(int connectfd)
{
    cJSON *sendjson = cJSON_CreateObject();
    cJSON_AddStringToObject(sendjson, "result", "failed");
    char *sendstr = cJSON_Print(sendjson);
    int ret = send(connectfd, sendstr, strlen(sendstr) + 1, 0);
    cJSON_Delete(sendjson);
    return ret;
}

int socketconnect(int socketfd)
{
    struct tcp_info info;
    int len = sizeof(info);
    getsockopt(socketfd, IPPROTO_TCP, TCP_INFO, &info, (socklen_t*)&len);
    if ((info.tcpi_state == TCP_ESTABLISHED))
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

void sig_pipe(int signo)
{
    start_analysis = 0;
    disconnect = 1;
}

int setParam(cJSON *json)
{
    //循环读取json里面的数据
    //如果配置文件里面有对应的参数就修改，没有就添加
    int i;
    int model_ = 0;
    int had_new = 0;
    cJSON *json_model = cJSON_GetObjectItem(json, "model");
    char conf_file[100]={};
    KVConfig *m_cfg = NULL;

    if(json_model == NULL)
        return -1;

    if(json_model->valueint == 0)
    {
        m_cfg = cfg_;
        strcpy(conf_file, "/home/teacher_detect_trace.config");
    }
    else if(json_model->valueint == 1)
    {
        strcpy(conf_file, "/home/bd_detect_trace.config");
        if(cfg_db == NULL)
        {
            m_cfg = new KVConfig("/home/bd_detect_trace.config");
            had_new = 1;
        }
        else
        {
            m_cfg = cfg_db;
        }
    }
    else if(json_model->valueint == 2)
    {
        strcpy(conf_file, "/home/student_detect_trace.config");
        if(cfg_stu == NULL)
        {
            m_cfg = new KVConfig("/home/student_detect_trace.config");
            had_new = 1;
        }
        else
        {
            m_cfg = cfg_db;
        }
    }
    else
    {
        return -1;
    }

    int num = cJSON_GetArraySize(json);
    for (i = 2; i < num; i++)
    {
        cJSON *p = cJSON_GetArrayItem(json, i);

        if (p->type == cJSON_Number)
        {
            printf("%s\n",p->string);
            m_cfg->set_value(p->string, p->valueint);
        }
        else if(p->type == cJSON_String )
        {
            m_cfg->set_value(p->string, p->valuestring);
        }
    }

    m_cfg->save_as(conf_file);
    if(had_new == 1)
    {
        delete m_cfg;
    }
/*
    if(json_model->valueint == 0)
        g_teacher_cfg_changed = 1;
    else if(json_model->valueint == 1)
        g_bd_cfg_changed = 1;
    else if(json_model->valueint == 2)
        g_stu_cfg_changed = 1;
*/
    return 1;
}
void sendVersion(int connectfd)
{
    cJSON *sendjson = cJSON_CreateObject();
    cJSON_AddStringToObject(sendjson, "version", version);
    char *sendstr = cJSON_Print(sendjson);
    int ret = send(connectfd, sendstr, strlen(sendstr) + 1, 0);
    cJSON_Delete(sendjson);
}

void sendConf(int connectfd, int blackboard_conf)
{
    cJSON *sendjson = cJSON_CreateObject();
    std::vector<std::string>::iterator iter;
    std::vector<std::string> cfg_keys;
    KVConfig *sendcfg_;
    if(blackboard_conf == 0)
    {
        sendcfg_ = new KVConfig("/home/teacher_detect_trace.config");
    }
    else
    {
        sendcfg_ = new KVConfig("/home/bd_detect_trace.config");
    }
    cfg_keys = sendcfg_->keys();
    for(iter = cfg_keys.begin();iter != cfg_keys.end(); iter++)
    {
        const char *str = (*iter).c_str();
        //std::cout<<(*iter)<<endl;
        cJSON_AddStringToObject(sendjson, str, sendcfg_->get_value(str));
    }
    char *sendstr = cJSON_Print(sendjson);
    int ret = send(connectfd, sendstr, strlen(sendstr) + 1, 0);
    cJSON_Delete(sendjson);
    delete sendcfg_;
}
void sendModelandStatus(int connectfd)
{
    cJSON *sendjson = cJSON_CreateObject();
    cJSON_AddStringToObject(sendjson, "model", cfg_->get_value("model"));
    cJSON_AddStringToObject(sendjson, "status", start_analysis == 1 ? "1" : "0");
    char *sendstr = cJSON_Print(sendjson);
    int ret = send(connectfd, sendstr, strlen(sendstr) + 1, 0);
    cJSON_Delete(sendjson);
}

cv::Point GetTransformPoint(cv::Point src, cv::Mat warp)
{
    Point ret;
    ret.x = (warp.at<float>(0, 0) * src.x + warp.at<float>(0, 1) * src.y + warp.at<float>(0, 2)) / (warp.at<float>(2, 0) * src.x + warp.at<float>(2, 1) * src.y + warp.at<float>(2, 2));
    ret.y = (warp.at<float>(1, 0) * src.x + warp.at<float>(1, 1) * src.y + warp.at<float>(1, 2)) / (warp.at<float>(2, 0) * src.x + warp.at<float>(2, 1) * src.y + warp.at<float>(2, 2));
    return ret;
}

void recvResultfromSlave()
{
    int master_recv_sockfd;
    struct sockaddr_in sin;
    char message[256];
    memset(&sin, 0, sizeof(struct sockaddr_in));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_port = htons(9001);
    int sin_len = sizeof(sin);

    master_recv_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    //设置socket为阻塞模式
    int flags = fcntl(master_recv_sockfd, F_GETFL, 0);
    fcntl(master_recv_sockfd, F_SETFL, flags & ~O_NONBLOCK);

    bind(master_recv_sockfd, (struct sockaddr *)&sin, sizeof(sin));
    pthread_cleanup_push(recvResultfromSlaveCleanup, (void*) master_recv_sockfd);
    while(1)
    {
        int ret = recvfrom(master_recv_sockfd, message, sizeof(message), 0, (struct sockaddr *)&sin, (socklen_t*)&sin_len);
        if(ret ==  -1)
        {
            //close(master_recv_sockfd);
            pthread_exit((void*)-1);
        }
        Point p = getPoint(message);
        //Point p;
        if(slave_queue.size() >= 5)
        {
            slave_queue.pop();
        }
        slave_queue.push(p);
        usleep(10);
    }
    pthread_cleanup_pop(0);
}

void recvResultfromSlaveCleanup(void *arg)
{
    close((int)arg);
}

cv::Point2f* getCorrPoint(const char *str, cv::Point2f *point)
{
    char *data = strdup(str);
    char *p = strtok(data, ";");
    int i;
    for(i = 0; i < 4; i++)
    {
        int x, y;
        if(sscanf(p, "%d,%d",&x,&y) == 2)
        {
            point[i].x = (float)x;
            point[i].y = (float)y;
        }
        p = strtok(0, ";");
    }
    free(data);
    return point;
}

cv::Point getPoint(const char* message)
{
    Point p;
    return p;
}
