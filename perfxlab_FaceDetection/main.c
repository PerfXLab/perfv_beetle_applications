/*
 * Copyright 2019 GreenWaves Technologies, SAS
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "stdio.h"

/* PMSIS includes */
#include "pmsis.h"

/* PMSIS BSP includes */
#include "bsp/gapuino.h"
#include "bsp/camera/ov7725.h"
#include "bsp/display/yt280L030.h"

#include "faceDet.h"
#include "FaceDetKernels.h"
#include "ImageDraw.h"
#include "setup.h"

#define USE_FACEDET

#define CAM_WIDTH    320
#define CAM_HEIGHT   240

#define LCD_WIDTH    320
#define LCD_HEIGHT   240

static unsigned short *camBuff0;
static unsigned char *imgBuff0;
static struct pi_device lcd;
static pi_buffer_t buffer_cam;
static pi_buffer_t buffer;
static pi_buffer_t buffer_out;
static struct pi_device cam;

L2_MEM unsigned char *ImageOut;
L2_MEM unsigned int *ImageIntegral;
L2_MEM unsigned int *SquaredImageIntegral;
L2_MEM char str_to_lcd[100];

struct pi_device cluster_dev;
struct pi_cluster_task *task;
struct pi_cluster_conf conf;
ArgCluster_T ClusterCall;

void setCursor(struct pi_device *device,signed short x, signed short y);
void writeFillRect(struct pi_device *device, unsigned short x, unsigned short y, unsigned short w, unsigned short h, unsigned short color);
void writeText(struct pi_device *device,char* str,int fontsize);

/* ******************************************** */

/*
converted byte order.
*/
uint16_t  color_MSB_to_LSB (uint16_t color)
{
    uint16_t tmpcolor = 0;
    tmpcolor |= ((color >> 8) & 0xFF);
    tmpcolor |= ((color & 0xFF) << 8);
    color = tmpcolor;

    return color;
}

/* YT280 lcd convert methods */
#define GRAY_2_RGB565(gray) ((uint16_t)((((uint8_t)(gray)>>3)<<11)|(((uint8_t)(gray)>>2)<<5)|((uint8_t)(gray)>>3)))

/*
    YUV sample Y only (uint16_t) and copy to imagebuff0 (uint8_t)

    (uint16_t) --->  (uint8_t)
*/
uint8_t* yuv_only_sample_y(uint16_t * input_img ,uint8_t * output_img, uint16_t width,uint16_t high)
{
    uint16_t yuv_color = 0;
    uint8_t y_color = 0;

    for(uint16_t in_y = 0; in_y < high; in_y++)
    {
        for(uint16_t in_x = 0; in_x < width; in_x++)
        {
            yuv_color =  *(input_img + in_y * width + in_x);
            y_color = (yuv_color >> 8) & 0xFF;                      //(yuv_color & 0xFF);                       //shuld be (yuv_color >> 8) & 0xFF

            *(output_img + in_y * width + in_x) = y_color;
            //*(input_img + in_y * width + in_x) = color_MSB_to_LSB( GRAY_2_RGB565(y_color) );
        }
    }

    return output_img;
}

/*
    convert Y to RGB565

    output_img > input_img convert buff size (input_img * 2)

    if ( output_img-> pixel size ) == (input_img-> pixel size)
        then : output_width == input_width
    if ( output_img-> pixel size ) > (input_img-> pixel size)
        then : output_width must be the true width and high.
*/
uint16_t* conver_Y_to_RGB565(uint8_t * input_img ,uint16_t * output_img, uint16_t input_width,uint16_t input_high ,uint16_t output_width,uint16_t output_high )
{
    uint16_t rgb_color = 0;
    uint8_t y_color = 0;

    for(uint16_t in_y = 0; in_y < input_high; in_y++)
    {
        for(uint16_t in_x = 0; in_x < input_width; in_x++)
        {
            y_color =  *(input_img + in_y * input_width + in_x);
            *(output_img + in_y * output_width + in_x) = color_MSB_to_LSB( GRAY_2_RGB565(y_color) );
        }
    }

    return output_img;
}


/*
display_world(&lcd,crop_buff,crop_width, crop_high,LCD_WIDTH,LCD_HEIGHT,buff_x_off,buff_y_off);

display memory < lcd
*/
void display_world(struct pi_device *device, uint16_t* buff_imag,
        uint16_t buff_width,uint16_t buff_high  ,
        uint16_t lcd_width, uint16_t lcd_high,  
        uint16_t lcd_x_off, uint16_t lcd_y_off)
{
    if( ((lcd_x_off + buff_width) > lcd_width) || ((lcd_y_off + buff_high) > lcd_high  ) )
    {
        printf(" invalid parameter !!!\n ");
        return;
    }

   for(uint16_t off_y = 0; off_y < buff_high; off_y++)
   {
       for(uint16_t off_x=0; off_x < buff_width; off_x++)
       {    
           /* writeFillRect send uint16_t data, the order is first MSB and  second LSB
                but  pi_display_write send uint16_t data, the order is  first LSB and second MSB
                
                all data had converted  in  scale_buff_for_lcd() ,the order is first LSB and second MSB ,for pi_display_write().
                so, we shuld convert againd  for this.
            */
           writeFillRect(&lcd,(off_x + lcd_x_off),(off_y + lcd_y_off),1,1, color_MSB_to_LSB(*(buff_imag + (buff_width * off_y) + off_x ))  );
       }
   }
}

/* ******************************************** */

static int open_display(struct pi_device *device)
{
    struct pi_yt280_conf yt280_conf;

    pi_yt280_conf_init(&yt280_conf);

    pi_open_from_conf(device, &yt280_conf);

    if (pi_display_open(device))
    {
        return -1;
    }
    return 0;
}
static int open_camera_ov7725(struct pi_device *device)
{
  struct pi_ov7725_conf cam_conf;

  pi_ov7725_conf_init(&cam_conf);

  cam_conf.format = PI_CAMERA_QVGA;

  pi_open_from_conf(device, &cam_conf);
  if (pi_camera_open(device))
    return -1;

  return 0;
}

static int open_camera(struct pi_device *device)
{
    return open_camera_ov7725(device);
    return 0;
}


void test_facedetection(void)
{
    printf("Entering main controller...\n");

    unsigned int W = CAM_WIDTH, H = CAM_HEIGHT;
    unsigned int Wout = 64, Hout = 48;
    unsigned int ImgSize = W*H;

    pi_freq_set(PI_FREQ_DOMAIN_FC,250000000);

    camBuff0 = (unsigned short *)pmsis_l2_malloc((CAM_WIDTH*CAM_HEIGHT)*sizeof(unsigned short));
    if (camBuff0 == NULL)
    {
        printf("Failed to allocate Memory for cam Image \n");
        pmsis_exit(-1);
    }

    imgBuff0 = (unsigned char *)pmsis_l2_malloc((CAM_WIDTH*CAM_HEIGHT)*sizeof(unsigned char));
    if (imgBuff0 == NULL)
    {
        printf("Failed to allocate Memory for Image \n");
        pmsis_exit(-1);
    }

    //This can be moved in init
    ImageOut             = (unsigned char *) pmsis_l2_malloc((Wout*Hout)*sizeof(unsigned char));
    ImageIntegral        = (unsigned int *)  pmsis_l2_malloc((Wout*Hout)*sizeof(unsigned int));
    SquaredImageIntegral = (unsigned int *)  pmsis_l2_malloc((Wout*Hout)*sizeof(unsigned int));

    if (ImageOut == 0)
    {
        printf("Failed to allocate Memory for Image (%d bytes)\n", ImgSize*sizeof(unsigned char));
        pmsis_exit(-2);
    }
    if ((ImageIntegral == 0) || (SquaredImageIntegral == 0))
    {
        printf("Failed to allocate Memory for one or both Integral Images (%d bytes)\n", ImgSize*sizeof(unsigned int));
        pmsis_exit(-3);
    }
    printf("malloc done\n");

    if (open_display(&lcd))
    {
        printf("Failed to open display\n");
        pmsis_exit(-4);
    }
    printf("display done\n");

    if (open_camera(&cam))
    {
        printf("Failed to open camera\n");
        pmsis_exit(-5);
    }
    printf("Camera open success\n");

    buffer_cam.data = camBuff0;
    pi_buffer_init(&buffer_cam, PI_BUFFER_TYPE_L2, camBuff0);

    buffer_out.data = ImageOut;
    buffer_out.stride = 0;
    pi_buffer_init(&buffer_out, PI_BUFFER_TYPE_L2, ImageOut);
    pi_buffer_set_stride(&buffer_out, 0);

    pi_buffer_set_format(&buffer_cam, CAM_WIDTH, CAM_HEIGHT, 1, PI_BUFFER_FORMAT_RGB565);

    ClusterCall.ImageIn              = imgBuff0;
    ClusterCall.Win                  = W;
    ClusterCall.Hin                  = H;
    ClusterCall.Wout                 = Wout;
    ClusterCall.Hout                 = Hout;
    ClusterCall.ImageOut             = ImageOut;
    ClusterCall.ImageIntegral        = ImageIntegral;
    ClusterCall.SquaredImageIntegral = SquaredImageIntegral;

    pi_cluster_conf_init(&conf);
    pi_open_from_conf(&cluster_dev, (void*)&conf);
    pi_cluster_open(&cluster_dev);

    //Set Cluster Frequency to max
    pi_freq_set(PI_FREQ_DOMAIN_CL,175000000);

    task = (struct pi_cluster_task *) pmsis_l2_malloc(sizeof(struct pi_cluster_task));
    memset(task, 0, sizeof(struct pi_cluster_task));
    task->entry = (void *)faceDet_cluster_init;
    task->arg = &ClusterCall;

    pi_cluster_send_task_to_cl(&cluster_dev, task);

    task->entry = (void *)faceDet_cluster_main;
    task->arg = &ClusterCall;

    pi_display_ioctl(&lcd, PI_YT280_IOCTL_ORIENTATION, (void *) PI_YT280_ORIENTATION_90);

#ifdef USE_FACEDET
    //Setting Screen background to white
    writeFillRect(&lcd, 0, 0, 320, 240, 0xFFFF);
    setCursor(&lcd, 0, 0);
    writeText(&lcd,"---Perfxlab \n-----Perf-V Beetle", 2);
#endif
    printf("main loop start\n");

    int nb_frames = 0;
    while (1 && (NB_FRAMES == -1 || nb_frames < NB_FRAMES))
    {
        pi_camera_control(&cam, PI_CAMERA_CMD_START, 0);
        pi_camera_capture(&cam, camBuff0, CAM_WIDTH*CAM_HEIGHT*sizeof(unsigned short));
        pi_camera_control(&cam, PI_CAMERA_CMD_STOP, 0);

        yuv_only_sample_y(camBuff0 ,imgBuff0, CAM_WIDTH,CAM_HEIGHT);
        conver_Y_to_RGB565(imgBuff0 ,camBuff0, CAM_WIDTH , CAM_HEIGHT , CAM_WIDTH , CAM_HEIGHT );

#ifdef USE_FACEDET

        pi_cluster_send_task_to_cl(&cluster_dev, task);
        printf("end of face detection, faces detected: %d\n", ClusterCall.num_reponse);
        sprintf(str_to_lcd, "Face detected: %d\n", ClusterCall.num_reponse);
            setCursor(&lcd, 0, 220);
            writeText(&lcd, str_to_lcd, 2);

        //display_world(&lcd,camBuff0,CAM_WIDTH, CAM_HEIGHT,LCD_WIDTH,LCD_HEIGHT,0,0);
        pi_display_write(&lcd, &buffer_cam, 0, 0,LCD_WIDTH , LCD_HEIGHT-40);
        
        //pi_display_write(&lcd, &buffer_out, 40, 40, 160, 120);
        if (ClusterCall.num_reponse)
        {
            while(1);
            // sprintf(str_to_lcd, "Face detected: %d\n", ClusterCall.num_reponse);
            // setCursor(&lcd, 0, 220);
            // writeText(&lcd, str_to_lcd, 2);
            //sprintf(str_to_lcd,"1 Image/Sec: \n%d uWatt @ 1.2V   \n%d uWatt @ 1.0V   %c", (int)((float)(1/(50000000.f/ClusterCall.cycles)) * 28000.f),(int)((float)(1/(50000000.f/ClusterCall.cycles)) * 16800.f),'\0');
            //sprintf(out_perf_string,"%d  \n%d  %c", (int)((float)(1/(50000000.f/cycles)) * 28000.f),(int)((float)(1/(50000000.f/cycles)) * 16800.f),'\0');
        }

        nb_frames++;
#else
        //display_world(&lcd,camBuff0,CAM_WIDTH, CAM_HEIGHT,LCD_WIDTH,LCD_HEIGHT,0,0);
        pi_display_write(&lcd, &buffer_cam, 0, 0,LCD_WIDTH , LCD_HEIGHT);
#endif 

    }
    printf("Test face detection done.\n");
    pmsis_exit(0);
}

int main(void)
{
    printf("\n\t*** PMSIS FaceDetection Test ***\n\n");
    return pmsis_kickoff((void *) test_facedetection);
}
