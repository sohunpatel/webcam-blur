#include "opencv2/core.hpp"
#include "opencv2/core/base.hpp"
#include "opencv2/core/hal/interface.h"
#include "opencv2/core/types.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"
#include <exception>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <opencv4/opencv2/opencv.hpp>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef VID_WIDTH
#define VID_WIDTH 640
#endif

#ifndef VID_HEIGHT
#define VID_HEIGHT 480
#endif

#ifndef GRAB_CUT_ITER
#define GRAB_CUT_ITER 1
#endif

#define RECT_WIDTH 200
#define RECT_HEIGHT 150

#define VIDEO_IN "/dev/video0"
#define VIDEO_OUT "/dev/video6"

int main(int argc, char *argv[]) {
    // open and configure input camera (/dev/video0)
    cv::VideoCapture cam(VIDEO_IN);
    if (not cam.isOpened()) {
        std::cerr << "ERROR: could not open camera!\n";
        return -1;
    }
    cam.set(cv::CAP_PROP_FRAME_WIDTH, VID_WIDTH);
    cam.set(cv::CAP_PROP_FRAME_HEIGHT, VID_HEIGHT);

    // open output device
    int output = open(VIDEO_OUT, O_RDWR);
    if (output < 0) {
        std::cerr << "ERROR: could not open output device!\n" << strerror(errno);
        return -2;
    }

    // configure params for output device
    struct v4l2_format vid_format;
    memset(&vid_format, 0, sizeof(vid_format));
    vid_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

    if (ioctl(output, VIDIOC_G_FMT, &vid_format) < 0) {
        std::cerr << "ERROR: unable to get video format!\n" << strerror(errno);
        return -1;
    }

    size_t framesize = VID_WIDTH * VID_HEIGHT * 3;
    vid_format.fmt.pix.width = cam.get(cv::CAP_PROP_FRAME_WIDTH);
    vid_format.fmt.pix.height = cam.get(cv::CAP_PROP_FRAME_HEIGHT);

    // NOTE: change this according to below filters...
    // Chose one from the supported formats on Chrome:
    // - V4L2_PIX_FMT_YUV420,
    // - V4L2_PIX_FMT_Y16,
    // - V4L2_PIX_FMT_Z16,
    // - V4L2_PIX_FMT_INVZ,
    // - V4L2_PIX_FMT_YUYV,
    // - V4L2_PIX_FMT_RGB24,
    // - V4L2_PIX_FMT_MJPEG,
    // - V4L2_PIX_FMT_JPEG
    vid_format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;

    vid_format.fmt.pix.sizeimage = framesize;
    vid_format.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(output, VIDIOC_S_FMT, &vid_format) < 0) {
        std::cerr << "ERROR: unable to set video format!\n" << strerror(errno);
        return -1;
    }

    // create gui
    // const char *back = "backgound";
    // cv::namedWindow(back);
    // cv::setWindowTitle(back, "OpenCV Test - Background");
    // const char *fore = "foreground";
    // cv::namedWindow(fore);
    // cv::setWindowTitle(fore, "OpenCV Test - Foreground");

    int i = 0;
    cv::Mat empty(cv::Size(VID_WIDTH, VID_HEIGHT), CV_8UC1);
    cv::Mat result, foreground, background, bg_blur;
    foreground = empty;
    background = empty;
    const cv::Size blur_size(201, 201);

    // loop over these actions:
    while (true) {

        // grab frame
        cv::Mat frame;
        if (not cam.grab()) {
            std::cerr << "ERROR: could not read from camera!\n";
            break;
        }
        cam.retrieve(frame);

        // apply simple filter (NOTE: result should be as defined PIXEL FORMAT)
        // convert twice because we need RGB24
        // cv::Mat result, foreground, background, bg_blur;
        cv::cvtColor(frame, result, cv::COLOR_RGB2GRAY);
        // cv::cvtColor(result, result, cv::COLOR_GRAY2RGB);

        // setup variables needed
        cv::Rect rect(VID_WIDTH / 2 - RECT_WIDTH / 2, VID_HEIGHT / 2 - RECT_HEIGHT, VID_WIDTH,
                      VID_HEIGHT);
        cv::Mat bgdModel, fgdModel;
        cv::Mat mask(frame.size(), CV_8U, cv::Scalar(cv::GC_PR_BGD));
        mask.create(VID_HEIGHT, VID_WIDTH, CV_8UC1);

        // run grab cut algo
        try {
            if (i == 0) {
                cv::grabCut(frame, mask, rect, bgdModel, fgdModel, GRAB_CUT_ITER,
                            cv::GC_INIT_WITH_RECT);
                i++;
            } else {
                cv::grabCut(frame, mask, rect, bgdModel, fgdModel, GRAB_CUT_ITER,
                            cv::GC_INIT_WITH_RECT);
                i++;
            }
            cv::Mat maskFg = mask == cv::GC_FGD;
            cv::Mat maskPrFg = mask == cv::GC_PR_FGD;
            cv::Mat maskBg = mask == cv::GC_BGD;
            cv::Mat maskPrBg = mask == cv::GC_PR_BGD;
            frame.copyTo(background, maskBg | maskPrBg);
            frame.copyTo(foreground, maskFg | maskPrFg);
            // std::cout << mask << std::endl;
            cv::GaussianBlur(background, bg_blur, blur_size, cv::BORDER_DEFAULT);
            foreground.copyTo(result, maskPrFg | maskFg);
            bg_blur.copyTo(result, maskBg | maskPrBg);
            cv::imshow("result", result);
        } catch (std::exception &e) {
            std::cout << "Caught exception: " << i << std::endl;
            std::cout << e.what();
            break;
        }

        // write frame to output device
        try {
            size_t written = write(output, result.data, framesize);
            if (written < 0) {
                std::cerr << "ERROR: could not write to output device!\n";
                close(output);
                break;
            }
        } catch (std::exception &e) {
            std::cout << "Caught exception for writing to virtual device." << std::endl;
            std::cout << e.what() << std::endl;
        }

        // wait for user to finish program pressing ESC
        if (cv::waitKey(10) == 27)
            break;
    }

    std::cout << "\n\nFinish, bye!\n";
}
