function [raw, data] = low_level_blocks(opts)
%LOW_LEVEL_BLOCKS One block of samples through the low-level objects of libiio.
%   [RAW, DATA] = LOW_LEVEL_BLOCKS() does by hand what iio.RxStream does in one
%   call: on the ADALM-Pluto at ip:192.168.2.1 it enables the I/Q channels voltage0
%   and voltage1 of cf-ad9361-lpc in a channels mask, opens the buffer, creates a
%   block of 4096 samples, queues it, starts the stream and takes the block back.
%   RAW is the content of the block as a uint8 column, DATA the same samples
%   converted to a complex int16 column.
%
%   [RAW, DATA] = LOW_LEVEL_BLOCKS(Name = Value) changes Uri, Device, Channels
%   (I then Q) or Samples. On the emulated context of the tests (EmuFixture.m):
%       [raw, data] = low_level_blocks(Uri = fx.Uri, Device = "emu-rx", ...
%           Channels = ["voltage0_i" "voltage0_q"], Samples = 1000);
%
%   See also iio.ChannelsMask, iio.BufferStream, iio.Block, rx_capture.
arguments
    opts.Uri (1,1) string = "ip:192.168.2.1"
    opts.Device (1,1) string = "cf-ad9361-lpc"
    opts.Channels (1,:) string = ["voltage0" "voltage1"]
    opts.Samples (1,1) double {mustBeInteger, mustBePositive} = 4096
end
ctx = iio.Context(opts.Uri);
dev = ctx.find_device(opts.Device);
mask = iio.ChannelsMask(dev);
for id = opts.Channels
    enable(dev.find_channel(id), mask);     % iio:EINVAL when the device has no such input channel
end
bufs = dev.buffers;
bs = bufs(1).open(mask);                    % an iio.BufferStream, not started yet
blk = bs.create_block(opts.Samples * bs.sample_size);   % the size is in bytes
blk.enqueue();                              % hand the block to the device,
bs.start();                                 % start capturing,
blk.dequeue();                              % and wait until the block is full
raw = blk.bytes();
data = blk.read(OutputDataType = "int16", Complex = true);
% Release in the reverse order (leaving the function would do it as well).
blk.destroy();
bs.close();
mask.destroy();
delete(ctx);
end
