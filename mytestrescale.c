// tutorial01.c
//
// This tutorial was written by Stephen Dranger (dranger@gmail.com).
//
// Code based on a tutorial by Martin Bohme (boehme@inb.uni-luebeckREMOVETHIS.de)
// Tested on Gentoo, CVS version 5/01/07 compiled with GCC 4.1.1

// A small sample program that shows how to use libavformat and libavcodec to
// read video from a file.
//
// Use the Makefile to build all examples.
//
//      or gcc -Wall -ggdb tutorial01.c -D_GNU_SOURCE=1 -D_REENTRANT -I/usr/local/include -I/usr/include/SDL -c -o obj/tutorial01.o
//
// Run using
//
// tutorial01 myvideofile.mpg
//
// to write the first five frames from "myvideofile.mpg" to disk in PPM format.

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>

#include <stdio.h>

static uint8_t *video_dst_data[4] = {NULL};
static int video_dst_linesize[4];
static int video_dst_buf_size;
static enum AVPixelFormat pix_fmt;

//查找fmt中的某个media类型的stream，并得到stream_idx和dec_ctx的值
static int open_codec_context(int *stream_idx, AVCodecContext **dec_ctx, AVFormatContext *fmt_ctx, enum AVMediaType type)
{
  int ret, stream_index;
  AVStream *st;
  AVCodec *dec = NULL;
  AVDictionary *opts = NULL;

  ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
  if (ret < 0)
  {
    fprintf(stderr, "Could not find %s stream in input file",
            av_get_media_type_string(type));
    return ret;
  }
  else
  {
    stream_index = ret;
    st = fmt_ctx->streams[stream_index];
    /*find decoder for the stream*/
    dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (dec == NULL)
    {
      fprintf(stderr, "Failed to find %s codec\n",
              av_get_media_type_string(type));
      return AVERROR(EINVAL);
    }

    //为codec构建codeccontext
    *dec_ctx = avcodec_alloc_context3(dec);
    if (*dec_ctx == NULL)
    {
      fprintf(stderr, "Failed to allocate the %s codec context\n",
              av_get_media_type_string(type));
      return AVERROR(ENOMEM);
    }
    //设置codec_ctx的codecpar
    ret = avcodec_parameters_to_context(*dec_ctx, st->codecpar);
    if (ret < 0)
    {
      fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n",
              av_get_media_type_string(type));
      return ret;
    }

    //再open codec
    ret = avcodec_open2(*dec_ctx, dec, NULL);
    *stream_idx = stream_index;
    return ret;
  }
}

//处理解码后的frame,将frame的数据转化成rgb24，并写成ppm文件格式
static int handle_video_frame(AVFrame *frame)
{
  /* copy decoded frame to destination buffer:
     * this is required since rawvideo expects non aligned data */

  av_image_copy(video_dst_data, video_dst_linesize, (const uint8_t **)frame->data, frame->linesize, pix_fmt, frame->width, frame->height);

  enum AVPixelFormat dst_pix_fmt = AV_PIX_FMT_RGB24;

  struct SwsContext *sws_ctx = sws_getContext(frame->width, frame->height, pix_fmt, frame->width, frame->height, 
  dst_pix_fmt, SWS_BILINEAR, NULL, NULL, NULL);
  
  uint8_t *dst_data[4];
  int dst_linesize[4];
  int dst_frame_buf_size;
  
  dst_frame_buf_size = av_image_alloc(dst_data, dst_linesize, frame->width, frame->width, dst_pix_fmt, 1);
  sws_scale(sws_ctx, (const uint8_t * const*)frame->data, frame->linesize,0, frame->height, dst_data, dst_linesize);

  FILE *video_dst_file = fopen("/Users/chenzifei/Documents/workspace/rtsp/test2.ppm", "wb");
  fprintf(video_dst_file, "P6\n%d %d\n255\n", frame->width, frame->height);
  // int y = 0;
  // for(y = 0; y < frame->height; y++){
  //   fwrite(dst_data[0]+y*dst_linesize[0], 1, frame->width*3, video_dst_file);
  // }
  fwrite(dst_data[0], 1, dst_frame_buf_size, video_dst_file);
  fflush(video_dst_file);
  return 0;
}

//从pkt中进行解码
static int decode_packet(AVCodecContext *dec, AVPacket *pkt, AVFrame *frame)
{
  int ret = 0;
  //submit the packet to the decoder
  ret = avcodec_send_packet(dec, pkt);
  if (ret < 0)
  {
    fprintf(stderr, "Error submitting a packet for decoding (%s)\n", av_err2str(ret));
    return ret;
  }

  while (ret >= 0)
  {
    ret = avcodec_receive_frame(dec, frame);
    if (ret < 0)
    {
      // those two return values are special and mean there is no output
      // frame available, but there were no errors during decoding
      if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
        return 0;

      fprintf(stderr, "Error during decoding (%s)\n", av_err2str(ret));
      return ret;
    }

    // write the frame data to output file
    if (dec->codec->type == AVMEDIA_TYPE_VIDEO)
    {
      ret = handle_video_frame(frame);
    }
    else
    {
      ; //TODO
    }

    av_frame_unref(frame);
    if (ret < 0)
    {
      return ret;
    }
  }

  return 0;
}

int main(int argc, char *argv[])
{
  const char *src_filename = "/Users/chenzifei/Documents/workspace/rtsp/out251.flv";
  const char *video_dst_filename = "/Users/chenzifei/Documents/workspace/rtsp/testout.flv";
  AVFormatContext *fmt_ctx = NULL;

  //open video
  int ret = avformat_open_input(&fmt_ctx, src_filename, NULL, NULL);
  if (ret < 0)
  {
    fprintf(stderr, "failed to open input file %s\n", src_filename);
    exit(1);
  }

  //从文件中读取部分数据，从而来获取streaminfo

  ret = avformat_find_stream_info(fmt_ctx, NULL);
  if (ret < 0)
  {
    fprintf(stderr, "failed to find stream info from file %s\n", src_filename);
    exit(1);
  }

  int video_stream_idx, audio_stream_idx;
  AVCodecContext *video_codec_ctx;
  AVStream *video_stream;
  int width, height;
  //申请一些buffer，用来存放图像数据,用av_image_alloc来帮我们自动生成大小
  ret = open_codec_context(&video_stream_idx, &video_codec_ctx, fmt_ctx, AVMEDIA_TYPE_VIDEO);
  if (ret >= 0)
  {
    //video stream发现了
    video_stream = fmt_ctx->streams[video_stream_idx];
    width = video_codec_ctx->width;
    height = video_codec_ctx->height;
    pix_fmt = video_codec_ctx->pix_fmt;

    ret = av_image_alloc(video_dst_data, video_dst_linesize, width, height, pix_fmt, 1);
    if (ret < 0)
    {
      fprintf(stderr, "failed to alloc image");
      exit(1);
    }

    video_dst_buf_size = ret;
  }

  //初始化pkt,将它的数据置为null
  AVPacket pkt;
  av_init_packet(&pkt);
  pkt.data = NULL;
  pkt.size = 0;

  //申请一个AVFrame对象
  AVFrame *frame = av_frame_alloc();
  if (frame == NULL)
  {
    fprintf(stderr, "failed to alloc avframe");
    exit(1);
  }

  //read frame from file
  while (av_read_frame(fmt_ctx, &pkt) >= 0)
  {
    if (pkt.stream_index == video_stream_idx)
    {
      decode_packet(video_codec_ctx, &pkt, frame);
    }
    else
    {
      ; //ignore
    }
  }
}
