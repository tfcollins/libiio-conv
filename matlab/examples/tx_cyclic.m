function frame = tx_cyclic(opts)
%TX_CYCLIC Transmit one frame over and over with a cyclic iio.TxStream.
%   FRAME = TX_CYCLIC() opens the ADALM-Pluto at ip:192.168.2.1, writes one frame
%   of 1024 samples holding 8 cycles of a complex tone at half of full scale to the
%   I/Q channels voltage0 and voltage1 of cf-ad9361-dds-core-lpc, lets the device
%   repeat it for 5 seconds and returns the frame (complex double).
%
%   FRAME = TX_CYCLIC(Name = Value) changes Uri, Device, Channels (I then Q),
%   SamplesPerFrame, Cycles, Amplitude (1 is full scale) or Seconds. On the
%   emulated context of the tests (matlab/tests/EmuFixture.m):
%       frame = tx_cyclic(Uri = fx.Uri, Device = "emu-tx", ...
%           Channels = ["voltage0_i" "voltage0_q"], Seconds = 0);
%
%   See also iio.TxStream, rx_capture.
arguments
    opts.Uri (1,1) string = "ip:192.168.2.1"
    opts.Device (1,1) string = "cf-ad9361-dds-core-lpc"
    opts.Channels (1,:) string = ["voltage0" "voltage1"]
    opts.SamplesPerFrame (1,1) double {mustBeInteger, mustBePositive} = 1024
    opts.Cycles (1,1) double {mustBeInteger} = 8
    opts.Amplitude (1,1) double = 0.5
    opts.Seconds (1,1) double {mustBeNonnegative} = 5
end
ctx = iio.Context(opts.Uri);
dev = ctx.find_device(opts.Device);     % 1-by-0 when the context has no such device
% Scale = "fullscale": the data is in -1 to 1. Cyclic: the first frame repeats.
tx = iio.TxStream(dev, opts.Channels, SamplesPerFrame = opts.SamplesPerFrame, ...
    Complex = true, Scale = "fullscale", Cyclic = true);
k = (0:opts.SamplesPerFrame - 1).';
% A whole number of cycles, so the repetition has no jump.
frame = opts.Amplitude * exp(2i * pi * opts.Cycles * k / opts.SamplesPerFrame);
if ~tx.write(frame)
    error('tx_cyclic:notSent', 'The frame was not accepted in time.');
end
pause(opts.Seconds);    % the frame repeats until the stream is closed
tx.close();             % a second write to a cyclic stream would raise iio:EBUSY
end
