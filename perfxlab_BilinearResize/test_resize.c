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

/* PMSIS includes. */
#include "pmsis.h"

/* App includes. */
#include "setup.h"

/* PMSIS BSP includes. */
#include "bsp/bsp.h"
#include "bsp/buffer.h"

//#include "bsp/gapuino.h"
#include "bsp/display.h"
#include "bsp/camera/ov7725.h"
#include "bsp/display/yt280L030.h"


/* Autotiler includes. */
#include "ResizeKernels.h"

/* Variables used. */
uint16_t *cam_image = NULL;
uint8_t *image_in = NULL;
uint8_t *image_out = NULL;
uint8_t *image_bw = NULL;

typedef struct ArgCluster
{
    uint32_t Win;
    uint32_t Hin;
    uint32_t Wout;
    uint32_t Hout;
    uint8_t *ImageIn;
    uint8_t *ImageOut;
} ArgCluster_T;

#define HAVE_BRIDGE
/* ************************************************************************** 
 ************************************************************************** */

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
    convert Y to RGB565

    output_img > input_img convert buff size (input_img * 2)

    if ( output_img-> pixel size ) == (input_img-> pixel size)
        then : output_width == input_width
    if ( output_img-> pixel size ) > (input_img-> pixel size)
        then : output_width must be the true width and high.

    conver_Y_to_RGB565_off diff conver_Y_to_RGB565 :
        extra the off function.

*/
uint16_t* conver_Y_to_RGB565_off(uint8_t * input_img ,uint16_t * output_img,    \
                                uint16_t input_width,uint16_t input_high ,     \ 
                                uint16_t output_width,uint16_t output_high ,    \
                                uint16_t x_off,uint16_t y_off)                  \
{
    uint16_t rgb_color = 0;
    uint8_t y_color = 0;

    for(uint16_t in_y = 0; in_y < input_high; in_y++)
    {
        for(uint16_t in_x = 0; in_x < input_width; in_x++)
        {
            y_color =  *(input_img + in_y * input_width + in_x);
            *(output_img + (in_y + y_off) * output_width + (in_x + x_off)) = color_MSB_to_LSB( GRAY_2_RGB565(y_color) );
        }
    }

    return output_img;
}

/*
display_world(&lcd,crop_buff,crop_width, crop_high,LCD_WIDTH,LCD_HEIGHT,buff_x_off,buff_y_off);

display memory < lcd
*/
#if 1
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
           writeFillRect(device,(off_x + lcd_x_off),(off_y + lcd_y_off),1,1, color_MSB_to_LSB(*(buff_imag + (buff_width * off_y) + off_x ))  );
       }
   }
}
#endif
/* **************************************************************************
 ************************************************************************** */

static void cluster_camera(ArgCluster_T *ArgC)
{
    /* Launching resize on cluster. */
    ResizeImage(ArgC->ImageIn, ArgC->ImageOut);
}


int ConvAt(short *In, short int *Filter, unsigned int X, unsigned int Y, unsigned int W, unsigned int H, unsigned int Norm)
{
    unsigned int i, j;
    int Acc = 0;
    unsigned int K = 5;

    for (i=0; i<K; i++) {
        for (j=0; j<K; j++) {
            Acc += In[(X+i)*W+Y+j]*Filter[K*i+j];
        }
    }
    return (gap_clip(gap_roundnorm_reg(Acc, Norm), 15));
}


void DumpPlane(char *Mess, short int *Plane, unsigned int W, unsigned int H)
{
    unsigned int i, j;

    printf("----------------- %s ------------------------\n", Mess);
    for (i=0; i<H; i++) {
        printf("%2d: ", i);
        for (j=0; j<W; j++) {
            printf("%4x ", (unsigned short) Plane[i*W+j]);
        }
        printf("\n");
    }
    printf("-----------------------------------------\n");
}

void DumpPaddedCoeff(char *Name, short int *C, unsigned int NTap, unsigned int NFilter)
{
    unsigned int i, j;
    printf("L2_MEM short int %s[] = {\n", Name);
    for (i=0; i<NFilter; i++) {
        for (j=0; j<NTap; j++) {
            printf("%d, ", C[i*NTap+j]);
        }
        printf("0,\n");
    }
    printf("};\n");
}

int CheckSum(short int *In, int Size)
{
    int i;
    int S=0;

    for (i=0; i<Size; i++) S += In[i];
    return S;
}

void Check(char *Mess, short int *Planes, int NPlane, int W, int H)
{
    int i;
    printf("Check sum for %s\n", Mess);

    for (i=0; i<NPlane; i++) {
        printf("\t%2d: %d\n", i, CheckSum(Planes + i*(W*H), W*H));
    }
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

void test_cam(void)
{
    printf("Entering main controller\n");

    char string_buffer[64];
    struct pi_device camera;
    struct pi_device display;
    struct pi_device cluster_dev;
    struct pi_cluster_conf cluster_conf;
    struct pi_cluster_task task = {0};
    task.stack_size = (uint32_t) STACK_SIZE;

    uint32_t cam_size = sizeof(uint16_t) * CAMERA_HEIGHT * CAMERA_WIDTH;
    uint32_t input_size = sizeof(uint8_t) * CAMERA_HEIGHT * CAMERA_WIDTH;
    uint32_t resize_size = sizeof(uint8_t) * RESIZE_HEIGHT * RESIZE_WIDTH;

    cam_image = (uint16_t *) pmsis_l2_malloc(cam_size);
    image_in = (uint8_t *) pmsis_l2_malloc(input_size);
    image_out = (uint8_t *) pmsis_l2_malloc(resize_size);

    if (cam_image == NULL)
    {
        printf("cam_image alloc failed: %d bytes !\n", cam_size);
        pmsis_exit(-1);
    }

    if (image_in == NULL)
    {
        printf("image_in alloc failed: %d bytes !\n", input_size);
        pmsis_exit(-1);
    }

    if (image_out == NULL)
    {
        printf("image_out alloc failed: %d bytes !\n", resize_size);
        pmsis_exit(-2);
    }

    if (open_display(&display))
    {
        printf("Failed to open display\n");
        pmsis_exit(-3);
    }

    pi_display_ioctl(&display, PI_YT280_IOCTL_ORIENTATION, (void *) PI_YT280_ORIENTATION_90);

    if (open_camera(&camera))
    {
        printf("Failed to open camera\n");
        pmsis_exit(-4);
    }

/*********************************************************************************/
    // #if defined(HAVE_BRIDGE)
    // char name[] = "../../../imgTest0.pgm";
    // uint32_t w_in = 0, h_in = 0;
    // if ((ReadImageFromFile(name, &w_in, &h_in, image_in, input_size) == 0) ||
    //     (w_in != CAMERA_WIDTH) || (h_in != CAMERA_HEIGHT))
    // {
    //     printf("Failed to load image %s or dimension mismatch Expects [%dx%d], Got [%dx%d]\n", name, CAMERA_WIDTH, CAMERA_HEIGHT, w_in, h_in);
    //     pmsis_exit(-5);
    // }
    // #endif  /* HAVE_BRIDGE */
/*********************************************************************************/

    printf("Initializing cluster\n");
    /* Configure And open cluster. */
    cluster_conf.id = 0;
    pi_open_from_conf(&cluster_dev, (void *) &cluster_conf);
    if (pi_cluster_open(&cluster_dev))
    {
        printf("Cluster open failed !\n");
        pmsis_exit(-6);
    }

    /* Allocating L1 memory for cluster */
    Resize_L1_Memory = (char *) pmsis_l1_malloc(_Resize_L1_Memory_SIZE);
    if (Resize_L1_Memory == 0)
    {
        printf("Failed to allocate %d bytes for L1_memory\n", _Resize_L1_Memory_SIZE);
        pmsis_exit(-7);
    }

    ArgCluster_T cluster_call;

    task.entry = (void *) cluster_camera;
    task.arg = &cluster_call;
    printf("Cluster done\n");


    static pi_buffer_t RenderBuffer;
    static pi_buffer_t ImageInBuffer;
    static pi_buffer_t CamImageBuffer;

    // WIth ov7725, propertly configure the buffer to skip boarder pixels
    CamImageBuffer.data = cam_image;
    pi_buffer_init(&CamImageBuffer, PI_BUFFER_TYPE_L2, cam_image);

    ImageInBuffer.data = image_in;
    pi_buffer_init(&ImageInBuffer, PI_BUFFER_TYPE_L2, image_in);

    pi_buffer_init(&RenderBuffer, PI_BUFFER_TYPE_L2, image_out);

    pi_buffer_set_format(&CamImageBuffer, CAMERA_WIDTH, CAMERA_HEIGHT, 1, PI_BUFFER_FORMAT_RGB565);
    pi_buffer_set_format(&ImageInBuffer, CAMERA_WIDTH, CAMERA_HEIGHT, 1, PI_BUFFER_FORMAT_GRAY);
    pi_buffer_set_format(&RenderBuffer, RESIZE_WIDTH, RESIZE_HEIGHT, 1, PI_BUFFER_FORMAT_GRAY);

    uint32_t loop = 1, index = 0;
    while (loop)
    {
        pi_camera_control(&camera, PI_CAMERA_CMD_START, 0);
        pi_camera_capture(&camera, cam_image, CAMERA_HEIGHT * CAMERA_WIDTH * sizeof(uint16_t));
        pi_camera_control(&camera, PI_CAMERA_CMD_STOP, 0);
        //printf("Capture done\n");

        yuv_only_sample_y(cam_image ,image_in, CAMERA_WIDTH,CAMERA_HEIGHT);
        //( output_img-> pixel size ) == (input_img-> pixel size)
        conver_Y_to_RGB565(image_in ,cam_image, CAMERA_WIDTH , CAMERA_HEIGHT , CAMERA_WIDTH , CAMERA_HEIGHT );

	   cluster_call.Win      = (uint32_t) CAMERA_WIDTH;
	   cluster_call.Hin      = (uint32_t) CAMERA_HEIGHT;
	   cluster_call.Wout     = (uint32_t) RESIZE_WIDTH;
	   cluster_call.Hout     = (uint32_t) RESIZE_HEIGHT;
	   cluster_call.ImageIn  = image_in;
	   cluster_call.ImageOut = image_out;
        pi_cluster_send_task_to_cl(&cluster_dev, &task);

#if 0
        //( output_img-> pixel size ) == (input_img-> pixel size)
        conver_Y_to_RGB565(image_out ,cam_image, RESIZE_WIDTH , RESIZE_HEIGHT , RESIZE_WIDTH , RESIZE_HEIGHT );
        display_world(&display,cam_image,RESIZE_WIDTH, RESIZE_HEIGHT,IMG_WIDTH,IMG_HEIGHT,LCD_OFF_X,LCD_OFF_Y);
        pi_time_wait_us(1000000);
#else
        //( output_img-> pixel size ) > (input_img-> pixel size)
        conver_Y_to_RGB565_off(image_out ,cam_image, RESIZE_WIDTH , RESIZE_HEIGHT , CAMERA_WIDTH , CAMERA_HEIGHT,LCD_OFF_X,LCD_OFF_Y );
        pi_display_write(&display, &CamImageBuffer, 0, 0, IMG_WIDTH, IMG_HEIGHT);
#endif


         #if defined(HAVE_BRIDGE)
        /* Original image. */
        sprintf(string_buffer, "../../../pics/num_%d.pgm", index);
        WriteImageToFile(string_buffer, CAMERA_WIDTH, CAMERA_HEIGHT, image_in);
        /* Resized image. */
        sprintf(string_buffer, "../../../pics/num_%d_resize.pgm", index);
        WriteImageToFile(string_buffer, RESIZE_WIDTH, RESIZE_HEIGHT, image_out);
        index++;
        #endif  /* HAVE_BRIDGE */

    }

    pmsis_exit(0);
}

int main()
{
    printf("\n\n\t *** PMSIS Resize Test ***\n\n");
    return pmsis_kickoff((void *) test_cam);
}
