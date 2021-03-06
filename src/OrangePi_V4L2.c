#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>

#include <unistd.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include <OrangePiV4L2/OrangePiV4L2.h>
#include <OrangePiV4L2/OrangePi_Config.h>
#include <OrangePiV4L2/OrangePi_Debug.h>
#include <OrangePiV4L2/YUYV2BMP.h>

/*
 * Open the device.
 */
static int OrangePi_open(struct OrangePi_v4l2_device *dev)
{
    int fd;
    
    if (strcmp(OrangePi_Get_Platform(), "OrangePi_RDA") == 0) 
        fd = open(dev->device_name, O_RDWR, 0);  
    else
        fd = open(dev->device_name, O_RDWR | O_NONBLOCK,0);
    
    if(fd < 0) {
    	DEBUG_ERROR("ERROR: Fail to open the video0 device\n");
	    return -1;
    }
    
    DEBUG_ORANGEPI("Succeed to open Camrea!\n");
    dev->fd = fd;
    return 0;
}

/*
 * Close the device.
 */
static void OrangePi_close(struct OrangePi_v4l2_device *dev)
{
    int i = 0;
    enum v4l2_buf_type type;

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(ioctl(dev->fd , VIDIOC_STREAMOFF , &type))
	    DEBUG_ORANGEPI("VIDIOC_STREAMOFF.\n");

unmap:
    for(; i < dev->buffers->n_buffers ; i++) {
	if(munmap(dev->buffers->Raw_buffers[i].start , dev->buffers->Raw_buffers[i].length)) {
	    DEBUG_ERROR("ERROR:Faild to free mmap area!\n");
	    goto unmap;
	}	
    }

    free(dev->buffers->Raw_buffers);
    free(dev->buffers);
    close(dev->fd);
    DEBUG_ORANGEPI("Close OrangePi Camera!\n");
}

/*
 * Get camera information that support.
 */
static int OrangePi_Camera_Capabilities(struct OrangePi_v4l2_device *dev)
{
    struct v4l2_capability cap;
    struct v4l2_fmtdesc fmtdesc;
    struct v4l2_format fmt;
    enum v4l2_buf_type type;

    DEBUG_ORANGEPI("OrangePi_Camera_Capabilities\n");
    if(ioctl(dev->fd,VIDIOC_QUERYCAP,&cap) < 0) {
	    DEBUG_ERROR("ERROR:Can't get support information of camera\n");
	    return -1;
    }

    if((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0) {
	    DEBUG_WARN("WARN:[%s]Video capture not support\n",__func__);
    }

    if((cap.capabilities & V4L2_CAP_STREAMING) == 0) 
	    DEBUG_WARN("WARN:[%s]Device don't support streaming i/o\n",__func__);

    /* Show support*/
    DEBUG_ORANGEPI("======= Support Information ==========\n");
    DEBUG_ORANGEPI("Driver name:%s\n",cap.driver);
    DEBUG_ORANGEPI("device name:%s\n",cap.card);
    DEBUG_ORANGEPI("Device Path:%s\n",cap.bus_info);
    DEBUG_ORANGEPI("Version:    %d.%d\n",(cap.version >> 16) && 0xff,(cap.version >> 24) && 0xff);
    DEBUG_ORANGEPI("Capabilitie:%u\n",cap.capabilities);

    fmtdesc.index = 0;
    fmtdesc.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
    DEBUG_ORANGEPI("======= Support Format ========\n");
    while(ioctl(dev->fd,VIDIOC_ENUM_FMT,&fmtdesc) != -1) {
	    DEBUG_ORANGEPI("[%d]%s\n",fmtdesc.index + 1,fmtdesc.description);
	    fmtdesc.index++;
    }

    return 0;
}

static void OrangePi_Current_Framer(struct OrangePi_v4l2_device *dev)
{
    struct v4l2_format fmt;
    struct v4l2_fmtdesc fmtdesc;

    DEBUG_ORANGEPI("OrangePi_Current_Framer\n");
    memset(&fmt,0,sizeof(struct v4l2_format));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(dev->fd,VIDIOC_G_FMT,&fmt);
    
    DEBUG_ORANGEPI("Current Framer information:\n"
             		"Width:%d\nHeight:%d\n",
	    fmt.fmt.pix.width,fmt.fmt.pix.height);

    memset(&fmtdesc,0,sizeof(struct v4l2_fmtdesc));

    fmtdesc.index = 0;
    fmtdesc.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    DEBUG_ORANGEPI("Support:\n");
    while(ioctl(dev->fd,VIDIOC_ENUM_FMT,&fmtdesc) != -1) {
        if(fmtdesc.pixelformat & fmt.fmt.pix.pixelformat) {
            DEBUG_ORANGEPI("Format:%s\n",fmtdesc.description);
            break;
        }
        fmtdesc.index++;
    }
}

/*
 * Check wether the camera support some format.
 */
static int OrangePi_Format_Support(struct OrangePi_v4l2_device *dev)
{
    DEBUG_ORANGEPI("OrangePi_Format_Support\n");
    return 0;
}

/*
 * OrangePi set video input.
 */
static int OrangePi_Set_input(struct OrangePi_v4l2_device *dev)
{
    struct v4l2_input input;
    int count = 0;

    if (strcmp(OrangePi_Get_Platform(), "OrangePi_RDA") == 0) {
        /* Empty operation */    
    } else {
        DEBUG_ORANGEPI("OrangePi_Set_input\n");
        memset(&input,0,sizeof(struct v4l2_input));
        input.index = count;
    
        while(!ioctl(dev->fd, VIDIOC_ENUMINPUT , &input))
    	    input.index = ++count;
        count -= 1;

        if(ioctl(dev->fd , VIDIOC_S_INPUT , &count) == -1) {
	        DEBUG_ERROR("ERROR:Selecting input %d\n",count);
	        return -1;
        }
    }
    return 0;
}

/*
 * Set the capture params.
 */
static int OrangePi_Set_Params(struct OrangePi_v4l2_device *dev)
{
    struct v4l2_format fmt;
    unsigned int min;

    DEBUG_ORANGEPI("OrangePi_Set_Params\n");
    memset(&fmt,0,sizeof(struct v4l2_format));
    if ((strcmp("OrangePi_H5", OrangePi_Get_Platform()) == 0) || 
        (strcmp("OrangePi_H3", OrangePi_Get_Platform()) == 0) ||
        (strcmp("OrangePi_H2", OrangePi_Get_Platform()) == 0)) {
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width  = dev->width;
        fmt.fmt.pix.height = dev->height;
        fmt.fmt.pix.field  = V4L2_FIELD_ANY;
        fmt.fmt.pix.pixelformat = dev->format;
        fmt.fmt.pix.sizeimage   = dev->width * dev->height * 3 / 2;
        fmt.fmt.pix.colorspace = 8;
    } else if (strcmp("OrangePi_RDA", OrangePi_Get_Platform()) == 0) {
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width  = dev->width;
        fmt.fmt.pix.height = dev->height;
        fmt.fmt.pix.field  = V4L2_FIELD_INTERLACED;
        fmt.fmt.pix.pixelformat = dev->format;
    } else {
        DEBUG_ERROR("Unknown Platform!\n");
        return -1;    
    }

    if(OrangePi_Format_Support(dev) < 0) {
	    DEBUG_ERROR("Pls use another mode.\n");
	    return -1;
    }

    if(ioctl(dev->fd, VIDIOC_S_FMT, &fmt) == -1) {
	    DEBUG_ERROR("ERROR:Faild to set format.\n");
	    return -1;
    }

    DEBUG_ORANGEPI("Select Camera Mode:\n"
	    "Width:  %d\n"
	    "Height: %d\n"
	    "Type: %d\n"
	    "PixFmt: %s\n",
	    fmt.fmt.pix.width,
	    fmt.fmt.pix.height,
        fmt.type,
	    (char *)&fmt.fmt.pix.pixelformat);

    dev->width  = fmt.fmt.pix.width;
    dev->height = fmt.fmt.pix.height;

    if(dev->format != fmt.fmt.pix.pixelformat) {
	    DEBUG_ERROR("ERROR:Format not accepted\n");
	    return -1;
    }

    return 0;
}

/*
 * Set Camera frame rate.
 */
static int OrangePi_Set_Frame_Rate(struct OrangePi_v4l2_device *dev)
{
    struct v4l2_streamparm setfps;

    DEBUG_ORANGEPI("OrangePi_Set_Frame_Rate\n");
    memset(&setfps, 0 , sizeof(struct v4l2_streamparm));
    setfps.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    setfps.parm.capture.timeperframe.numerator = 1;
    setfps.parm.capture.timeperframe.denominator = dev->fps;

    if(ioctl(dev->fd, VIDIOC_S_PARM , &setfps) < 0) {
	    DEBUG_ERROR("ERROR: Bad Setting fram rate.\n");
	    return -1;
    }
    dev->fps = setfps.parm.capture.timeperframe.denominator;
    DEBUG_ORANGEPI("Support fps:%d\n",dev->fps);
    
    return 0;
}

/*
 * Alloc Buffer.
 */
static int OrangePi_Set_Buffer(struct OrangePi_v4l2_device *dev)
{
    struct v4l2_requestbuffers req;
    int i;

    DEBUG_ORANGEPI("OrangePi_Set_Buffer\n");
    memset(&req,0,sizeof(struct v4l2_requestbuffers));
    req.count  = dev->buffers->n_buffers;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if(ioctl(dev->fd, VIDIOC_REQBUFS,&req) < 0) {
	    DEBUG_ERROR("ERROR:Can't get v4l2 buffer!\n");
	    return -1;
    }

    if(req.count < 2) {
	    DEBUG_ERROR("ERROR:Can't get full buffer!\n");
	    return -1;
    }

    /* Alloc memory */
    dev->buffers->Raw_buffers = calloc(req.count , sizeof(struct buffer));
    for(i = 0; i < req.count ; i++) {
	    struct v4l2_buffer buf;

	    memset(&buf,0,sizeof(struct v4l2_buffer));
	    buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	    buf.memory   = V4L2_MEMORY_MMAP;
	    buf.index    = i;
	
	    if(ioctl(dev->fd , VIDIOC_QUERYBUF , &buf) == -1) {
	        DEBUG_ERROR("ERROR:Can't get buffer from v4l2 driver.\n");
	        return -1;
	    }
	    dev->buffers->Raw_buffers[i].length = buf.length;
	    dev->buffers->Raw_buffers[i].start  = 
				    mmap(NULL,buf.length, PROT_READ | PROT_WRITE,
					    MAP_SHARED , dev->fd , buf.m.offset);
	    if(MAP_FAILED == dev->buffers->Raw_buffers[i].start) {
	        DEBUG_ERROR("ERROR: MAP_FAILED.\n");
	        return -1;
	    }
    }

    return 0;
}

/*
 * Prepare to capture frams.
 */
static int OrangePi_Prepare_Capture(struct OrangePi_v4l2_device *dev)
{
    unsigned int i;
    enum v4l2_buf_type type;
   
    DEBUG_ORANGEPI("OrangePi_Prepare_Capture\n"); 
    for(i = 0 ; i < dev->buffers->n_buffers ; i++) {
	    struct v4l2_buffer buf;
    
	    memset(&buf,0,sizeof(struct v4l2_buffer));
	    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	    buf.memory = V4L2_MEMORY_MMAP;
	    buf.index  = i;

	    if(ioctl(dev->fd , VIDIOC_QBUF , &buf) < 0) {
	        DEBUG_ERROR("ERROR:BAD VIDIOC_QBUF\n");
	        return -1;
	    }
    }
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(ioctl(dev->fd , VIDIOC_STREAMON , &type) < 0) {
	    DEBUG_ERROR("ERROR:BAD VIDIOC_STREAM\n");
	    return -1;
    }
    return 0;
}

/*
 * Capture one frame
 */
static int OrangePi_Capture(struct OrangePi_v4l2_device *dev)
{
    fd_set fds;
    struct timeval tv;
    unsigned int i;
    struct v4l2_buffer buf;
    
    DEBUG_ORANGEPI("OrangePi_Capture\n");
    memset(&tv , 0 , sizeof(struct timeval));
    FD_ZERO(&fds);
    FD_SET(dev->fd,&fds);
    
    tv.tv_sec  = dev->timeout;
    tv.tv_usec = 0;
    
    if(select(dev->fd + 1 , &fds , NULL , NULL , &tv) == -1) {
	    DEBUG_ERROR("ERROR:Waiting for Frame\n");
	    return -1;
    }
    
    memset(&buf , 0 , sizeof(struct v4l2_buffer));
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    /* Dequeue buffer */
    if(ioctl(dev->fd , VIDIOC_DQBUF , &buf) < 0) {
	    DEBUG_ERROR("ERROR:VIDIOC_DQBUF\n");
	    return -1;
    }

    dev->buffers->YUV_buffer = 
        (unsigned char *)(unsigned long)dev->buffers->Raw_buffers[buf.index].start;

    dev->buffers->current_length = dev->buffers->Raw_buffers[buf.index].length;

    /* Put buffers */
    if(ioctl(dev->fd , VIDIOC_QBUF , &buf) < 0) {
	    DEBUG_ERROR("ERROR:Bad Put Buffer\n");
	    return -1;
    }
    
    return 0;  
}

/*
 * Initialize the OrangePi device.
 */
static int OrangePi_init(struct OrangePi_v4l2_device *dev)
{
    DEBUG_ORANGEPI("OrangePi_init\n");
    dev->width   = OrangePi_Get_Capture_Width();
    dev->height  = OrangePi_Get_Capture_Height();
    dev->format  = OrangePi_Get_Capture_Format();
    dev->fps     = OrangePi_Get_Capture_FPS();
    dev->timeout = OrangePi_Get_Capture_Timeout();
    dev->device_name = OrangePi_Get_Device_Name();

    dev->buffers = (struct OrangePi_buffer *)malloc(
		    sizeof(struct OrangePi_buffer));
    
    dev->buffers->n_buffers = OrangePi_Get_Buffer_Number();
    
    OrangePi_open(dev);
    OrangePi_Camera_Capabilities(dev);
    OrangePi_Set_Params(dev);
    OrangePi_Set_input(dev);
    OrangePi_Current_Framer(dev);
    OrangePi_Set_Buffer(dev);
    OrangePi_Prepare_Capture(dev);
    return 0;
}

struct OrangePi_v4l2_driver OrangePi = {
    .init     = OrangePi_init,
    .open     = OrangePi_open,
    .close    = OrangePi_close,
    .check_format = OrangePi_Format_Support,
    .capable  = OrangePi_Camera_Capabilities,
    .capture  = OrangePi_Capture,
    .current_framer = OrangePi_Current_Framer,
};
