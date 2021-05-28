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
static AVCodecContext *src_codec_ctx;
//输出的一些ctx定义
static AVFormatContext *output_fmt_ctx = NULL;
static AVCodecContext *output_encode_ctx = NULL;
static AVStream *dst_st = NULL;
//输出前需要转换的sws
static struct SwsContext *sws_ctx;
static int64_t next_pts;

//一个可重复利用的frame
static AVFrame *dst_frame;

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

//处理解码后的frame,将frame的数据重新进行编码，生成packet
static int handle_video_frame(AVFrame *src_frame)
{
  /* copy decoded frame to destination buffer:
     * this is required since rawvideo expects non aligned data */
  
  //dst_codec
  if(output_fmt_ctx == NULL){
    const char* output_filename = "/Users/chenzifei/Documents/workspace/rtsp/myout.flv";
  avformat_alloc_output_context2(&output_fmt_ctx, NULL, NULL, output_filename);
    //初始化
    AVCodecContext *dst_codec_ctx;
  AVCodec *dst_codec;
  dst_codec = avcodec_find_encoder(AV_CODEC_ID_H264);
  dst_codec_ctx = avcodec_alloc_context3(dst_codec);
  AVCodecParameters *dstPar = avcodec_parameters_alloc();
  // avcodec_parameters_from_context(dstPar, src_codec_ctx);
  // avcodec_parameters_to_context(dst_codec_ctx, dstPar);
  dst_codec_ctx->framerate = src_codec_ctx->framerate;
  dst_codec_ctx->pix_fmt = src_codec_ctx->pix_fmt;
  dst_codec_ctx->width = src_codec_ctx->width / 2;
  dst_codec_ctx->height = src_codec_ctx->height / 2;
  dst_codec_ctx->profile = src_codec_ctx->profile;
  dst_codec_ctx->level = src_codec_ctx->level;
  dst_codec_ctx->gop_size = src_codec_ctx->gop_size;
  dst_codec_ctx->max_b_frames = src_codec_ctx->max_b_frames;
  dst_codec_ctx->mb_decision = src_codec_ctx->mb_decision;
  dst_codec_ctx->flags = src_codec_ctx->flags;
  AVStream *st = avformat_new_stream(output_fmt_ctx, NULL);
  st->time_base = (AVRational){1,25};
  dst_codec_ctx->time_base = st->time_base;
  AVDictionary *opt = NULL;
  avcodec_open2(dst_codec_ctx, dst_codec, &opt);
  output_encode_ctx = dst_codec_ctx;
  avcodec_parameters_from_context(st->codecpar, output_encode_ctx);

  av_dump_format(output_fmt_ctx, 0, output_filename, 1);

  dst_frame = av_frame_alloc();
  dst_frame->width = dst_codec_ctx->width;
  dst_frame->height = dst_codec_ctx->height;
  dst_frame->format = dst_codec_ctx->pix_fmt;
  av_frame_get_buffer(dst_frame, 0);
  dst_st= st;

  int ret = 0;
  AVOutputFormat *fmt = NULL;
  fmt = output_fmt_ctx->oformat;

  /* open the output file, if needed */
  if (!(fmt->flags & AVFMT_NOFILE)) {
      ret = avio_open(&output_fmt_ctx->pb, output_filename, AVIO_FLAG_WRITE);
      if (ret < 0) {
          fprintf(stderr, "Could not open '%s': %s\n", output_filename,
                  av_err2str(ret));
          return 1;
      }
  }

  ret = avformat_write_header(output_fmt_ctx, &opt);
  if(ret < 0){
    fprintf(stderr, "Error write header (%s)\n", av_err2str(ret));
      return ret;
  }
  sws_ctx = sws_getContext(src_codec_ctx->width, src_codec_ctx->height, pix_fmt, dst_codec_ctx->width, dst_codec_ctx->height, 
  dst_codec_ctx->pix_fmt, SWS_BILINEAR, NULL, NULL, NULL);

  //写frame数据，先将srcframe转换到dstframe中

  }
  if (av_frame_make_writable(dst_frame) < 0)
      exit(1);
  sws_scale(sws_ctx, (const uint8_t * const *)src_frame->data, src_frame->linesize, 0, src_frame->height, dst_frame->data, dst_frame->linesize);
  dst_frame->pts= next_pts++;

  //往codec中写数据
  int ret = 0;
  ret = avcodec_send_frame(output_encode_ctx, dst_frame);
  if (ret < 0) {
        fprintf(stderr, "Error sending a frame to the encoder: %s\n",
                av_err2str(ret));
        exit(1);
    }
  while(ret >= 0){
    AVPacket pkt = { 0 };
    ret = avcodec_receive_packet(output_encode_ctx, &pkt);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        else if (ret < 0) {
            fprintf(stderr, "Error encoding a frame: %s\n", av_err2str(ret));
            exit(1);
    }
    /* rescale output packet timestamp values from codec to stream timebase */
    av_packet_rescale_ts(&pkt, output_encode_ctx->time_base, dst_st->time_base);
    pkt.stream_index = dst_st->index;
    ret = av_interleaved_write_frame(output_fmt_ctx, &pkt);
    av_packet_unref(&pkt);
      if (ret < 0) {
          fprintf(stderr, "Error while writing output packet: %s\n", av_err2str(ret));
          exit(1);
    }
  }
  return ret == AVERROR_EOF ? 1 : 0;
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
    av_dump_format(fmt_ctx, video_stream_idx, src_filename, 0);
    width = video_codec_ctx->width;
    height = video_codec_ctx->height;
    pix_fmt = video_codec_ctx->pix_fmt;
    src_codec_ctx = video_codec_ctx;

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
  av_write_trailer(output_fmt_ctx);
  avio_closep(&output_fmt_ctx->pb);
}
