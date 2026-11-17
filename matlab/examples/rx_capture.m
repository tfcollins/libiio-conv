function data = rx_capture(opts)
%RX_CAPTURE Capture complex samples with iio.RxStream, the one-call receive path.
%   DATA = RX_CAPTURE() opens the ADALM-Pluto at ip:192.168.2.1, captures 4 frames
%   of 32768 samples from the I/Q channels voltage0 and voltage1 of cf-ad9361-lpc
%   and returns them as one complex int16 column.
%
%   DATA = RX_CAPTURE(Name = Value) changes Uri, Device, Channels (I then Q),
%   SamplesPerFrame or Frames. On the emulated context of the tests, whose data
%   file holds 4096 samples (matlab/tests/EmuFixture.m):
%       data = rx_capture(Uri = fx.Uri, Device = "emu-rx", ...
%           Channels = ["voltage0_i" "voltage0_q"], SamplesPerFrame = 1000, Frames = 3);
%
%   See also iio.RxStream, low_level_blocks.
arguments
    opts.Uri (1,1) string = "ip:192.168.2.1"
    opts.Device (1,1) string = "cf-ad9361-lpc"
    opts.Channels (1,:) string = ["voltage0" "voltage1"]
    opts.SamplesPerFrame (1,1) double {mustBeInteger, mustBePositive} = 32768
    opts.Frames (1,1) double {mustBeInteger, mustBePositive} = 4
end
ctx = iio.Context(opts.Uri);
dev = ctx.find_device(opts.Device);     % 1-by-0 when the context has no such device
% Capture starts here. Raw = false (the default): shifted, masked and sign-extended.
rx = iio.RxStream(dev, opts.Channels, SamplesPerFrame = opts.SamplesPerFrame, ...
    OutputDataType = "int16", Complex = true);
n = opts.SamplesPerFrame;
data = complex(zeros(n * opts.Frames, 1, 'int16'));
for f = 1:opts.Frames
    [frame, valid] = rx.read();         % n-by-1 complex int16; valid is false when none came in time
    if ~valid
        error('rx_capture:noFrame', 'Frame %d did not arrive in time.', f);
    end
    data((f - 1) * n + (1:n)) = frame;
end
rx.close();     % releases the buffer; deleting rx or leaving the function does the same
end
