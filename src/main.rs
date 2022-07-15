use std::io;

use v4l::buffer::Type;
use v4l::io::traits::{CaptureStream, OutputStream};
use v4l::prelude::*;
use v4l::video::{Capture, Output};

fn main() -> io::Result<()> {
    let source = "/dev/video0";
    println!("Using source device: {}\n", source);

    // Determine which device to use
    let sink = "/dev/video20";
    println!("Using sink device: {}\n", sink);

    // Allocate 4 buffers by default
    let buffer_count = 64;

    let cap = Device::with_path(source)?;
    println!("Active cap capabilities:\n{}", cap.query_caps()?);
    println!("Active cap format:\n{}", Capture::format(&cap)?);
    println!("Active cap parameters:\n{}", Capture::params(&cap)?);

    let out = Device::with_path(sink)?;
    println!("Active out capabilities:\n{}", out.query_caps()?);
    println!("Active out format:\n{}", Output::format(&out)?);
    println!("Active out parameters:\n{}", Output::params(&out)?);

    let source_fmt = Capture::format(&cap)?;
    let sink_fmt = Output::set_format(&out, &source_fmt)?;
    if source_fmt.width != sink_fmt.width
        || source_fmt.height != sink_fmt.height
        || source_fmt.fourcc != sink_fmt.fourcc
    {
        return Err(io::Error::new(
            io::ErrorKind::Other,
            "failed to enforce source format on sink device",
        ));
    }
    println!("New out format:\n{}", Output::format(&out)?);

    // Setup a buffer stream and grab a frame, then print its data
    let mut cap_stream = MmapStream::with_buffers(&cap, Type::VideoCapture, buffer_count)?;
    let mut out_stream = MmapStream::with_buffers(&out, Type::VideoOutput, buffer_count)?;

    // warmup
    CaptureStream::next(&mut cap_stream)?;

    loop {
        let (buf_in, buf_in_meta) = CaptureStream::next(&mut cap_stream)?;
        let (buf_out, buf_out_meta) = OutputStream::next(&mut out_stream)?;

        // Output devices generally cannot know the exact size of the output buffers for
        // compressed formats (e.g. MJPG). They will however allocate a size that is always
        // large enough to hold images of the format in question. We know how big a buffer we need
        // since we control the input buffer - so just enforce that size on the output buffer.
        let buf_out = &mut buf_out[0..buf_in.len()];

        let stride: usize = source_fmt.stride as usize;
        for i in 0..buf_in.len() {
            let opp: usize = stride - (i % stride);
            let dest = i - (i % stride) + opp - 1;
            buf_out[i] = buf_in[dest];
        }

        // buf_out.copy_from_slice(buf_in);
        buf_out_meta.field = 0;
        buf_out_meta.bytesused = buf_in_meta.bytesused;
    };
}
