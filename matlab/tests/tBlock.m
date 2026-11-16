classdef tBlock < IioTestCase
    %TBLOCK iio.Block: size and bytes, queue state, converted read and write, the
    %   per-channel read and write of iio.Channel, parents and destruction.
    %   Receive data is the ramp of the fixture: sample k is I = k - 2048, Q = 2047 - k,
    %   each in the low 12 bits of a 16-bit word (EmuFixture.rampWords).

    methods (Test)
        function sizes(tc)
            [blk, bs, v0i, v0q] = rxBlock(tc);
            tc.verifyEqual(bs.sample_size, 4);
            tc.verifyClass(blk.nbytes, 'double');
            tc.verifyEqual(blk.nbytes, 32);
            tc.verifyEqual(blk.first(v0i), 0);
            tc.verifyEqual(blk.first(v0q), 2);
        end

        function constructorForm(tc)
            bs = openRx(tc);
            blk = iio.Block(bs, 16);
            tc.verifyClass(blk, 'iio.Block');
            tc.verifySize(blk, [1 1]);
            tc.verifyEqual(blk.nbytes, 16);
            tc.verifyError(@() iio.Block(bs, 1), 'iio:EINVAL');     % less than one sample
            tc.verifyError(@() iio.Block([], 16), 'iio:EINVAL');    % no buffer stream
        end

        function inertForms(tc)
            % no handle, no gateway call: the forms object arrays are built from
            b = iio.Block();
            tc.verifyEqual(b.Handle, uint64(0));
            e = iio.Block.empty(1, 0);
            tc.verifySize(e, [1 0]);
            tc.verifyClass(e, 'iio.Block');
            tc.verifyEqual(iio.internal.handle_count(), 0);
        end

        function bytes(tc)
            blk = rxBlock(tc);
            b = blk.bytes(0, 4);
            tc.verifyClass(b, 'uint8');
            tc.verifyEqual(b, uint8([0; 8; 255; 7]));
            whole = blk.bytes();
            tc.verifyClass(whole, 'uint8');
            tc.verifySize(whole, [32 1]);
            tc.verifyEqual(whole(5:8), uint8([1; 24; 254; 7]));
            tc.verifyEqual(blk.bytes(28), whole(29:32));
            tc.verifyError(@() blk.bytes(30, 4), 'iio:EINVAL');
        end

        function readInt16Complex(tc)
            blk = rxBlock(tc);
            x = blk.read(OutputDataType = "int16", Complex = true);
            tc.verifyClass(x, 'int16');
            tc.verifySize(x, [8 1]);
            tc.verifyFalse(isreal(x));
            k = (0:7).';
            tc.verifyEqual(x, complex(int16(k - 2048), int16(2047 - k)));
        end

        function readDefaults(tc)
            blk = rxBlock(tc);
            x = blk.read();
            tc.verifyClass(x, 'double');
            tc.verifySize(x, [8 2]);
            tc.verifyTrue(isreal(x));
            k = (0:7).';
            tc.verifyEqual(x, [k - 2048, 2047 - k]);
        end

        function readFullscale(tc)
            blk = rxBlock(tc);
            x = blk.read(OutputDataType = "single", Scale = "fullscale", Complex = true);
            tc.verifyClass(x, 'single');
            tc.verifySize(x, [8 1]);
            tc.verifyFalse(isreal(x));
            tc.verifyEqual(x(1), complex(single(-1), single(2047 / 2048)));
        end

        function readRaw(tc)
            blk = rxBlock(tc);
            x = blk.read(OutputDataType = "int16", Raw = true, Complex = true);
            tc.verifyClass(x, 'int16');
            tc.verifySize(x, [8 1]);
            tc.verifyEqual(x(1), complex(int16(2048), int16(2047)));
            tc.verifyEqual(x(2), complex(int16(6145), int16(2046)));
        end

        function readSubset(tc)
            [blk, ~, v0i, v0q] = rxBlock(tc);
            x = blk.read(Channels = v0q, OutputDataType = "int16");
            tc.verifyClass(x, 'int16');
            tc.verifyTrue(isreal(x));
            tc.verifyEqual(x, int16((2047:-1:2040).'));
            % column k is channel k as passed
            y = blk.read(Channels = [v0q v0i], OutputDataType = "int16");
            tc.verifyEqual(y, [int16((2047:-1:2040).'), int16((-2048:-2041).')]);
        end

        function readSamples(tc)
            blk = rxBlock(tc);
            x = blk.read(Samples = 3);
            tc.verifySize(x, [3 2]);
            tc.verifyEqual(x(:, 1), [-2048; -2047; -2046]);
            tc.verifySize(blk.read(Samples = 100), [8 2]);
            tc.verifyError(@() blk.read(Samples = 0), 'iio:EINVAL');
            tc.verifyError(@() blk.read(Samples = 2.5), 'iio:EINVAL');
        end

        function readWhileEnqueued(tc)
            bs = openRx(tc);
            blk = bs.create_block(8 * bs.sample_size);
            blk.enqueue();
            tc.verifyError(@() blk.read(), 'iio:EPERM');
            tc.verifyError(@() blk.bytes(), 'iio:EPERM');
        end

        function dequeueBeforeStart(tc)
            bs = openRx(tc);
            blk = bs.create_block(8 * bs.sample_size);
            tc.verifyError(@() blk.dequeue(), 'iio:EPERM');     % not enqueued
            blk.enqueue();
            t = tic;
            tc.verifyError(@() blk.dequeue(), 'iio:EPERM');     % the buffer stream is not started
            tc.verifyLessThan(toc(t), 1);
        end

        function enqueueTwice(tc)
            bs = openRx(tc);
            blk = bs.create_block(8 * bs.sample_size);
            blk.enqueue();
            tc.verifyError(@() blk.enqueue(), 'iio:EPERM');
        end

        function dequeueReturnsTrue(tc)
            bs = openRx(tc);
            blk = bs.create_block(8 * bs.sample_size);
            blk.enqueue();
            bs.start();
            tf = blk.dequeue();
            tc.verifyClass(tf, 'logical');
            tc.verifyEqual(tf, true);
            tc.verifyError(@() blk.dequeue(), 'iio:EPERM');     % it is ours again
        end

        function scaleNeedsFloat(tc)
            [blk, ~, ~, v0q] = rxBlock(tc);
            tc.verifyError(@() blk.read(OutputDataType = "int16", Scale = "fullscale"), 'iio:EINVAL');
            tc.verifyError(@() blk.read(OutputDataType = "int12"), 'iio:EINVAL');
            tc.verifyError(@() blk.read(Complex = true, Channels = v0q), 'iio:EINVAL');     % half a pair
        end

        function channelRead(tc)
            [blk, ~, v0i] = rxBlock(tc);
            tc.verifyEqual(v0i.read(blk), int16((-2048:-2041).'));
            r = v0i.read(blk, true);
            tc.verifyClass(r, 'uint8');
            tc.verifyEqual(numel(r), 16);
            tc.verifyEqual(r(:), uint8([0; 8; 1; 24; 2; 40; 3; 56; 4; 72; 5; 88; 6; 104; 7; 120]));
            % a channel that is not enabled in the mask of the stream: empty, of its class
            v2 = v0i.device.find_channel("voltage2");
            e = v2.read(blk);
            tc.verifyClass(e, 'int16');
            tc.verifySize(e, [0 1]);
            e = v2.read(blk, true);
            tc.verifyClass(e, 'uint8');
            tc.verifySize(e, [0 1]);
        end

        function argumentsAreNotConverted(tc)
            % an argument that must be a block or a channel is refused when it is
            % something else, before the gateway is called
            [blk, bs, v0i] = rxBlock(tc);
            n = iio.internal.handle_count();
            tc.verifyValidationError(@() blk.first(bs));
            tc.verifyValidationError(@() blk.first("voltage0_i"));
            tc.verifyValidationError(@() v0i.read(bs));
            tc.verifyValidationError(@() v0i.read(5));
            tc.verifyValidationError(@() v0i.write(bs, int16(1)));
            tc.verifyValidationError(@() blk.read(Channels = "voltage0_i"));
            tc.verifyValidationError(@() blk.read(Channels = bs));
            tc.verifyValidationError(@() blk.write(int16([1 2]), Channels = bs));
            tc.verifyEqual(iio.internal.handle_count(), n);
            % a channel twice is an error of the gateway
            tc.verifyError(@() blk.read(Channels = [v0i v0i]), 'iio:EINVAL');
            tc.verifyError(@() blk.write(int16([1 2]), Channels = [v0i v0i]), 'iio:EINVAL');
            tc.verifyEqual(v0i.read(blk), int16((-2048:-2041).'));      % the block is as it was
        end

        function write(tc)
            bs = openTx(tc);
            blk = bs.create_block(4 * bs.sample_size);
            n = blk.write(complex(int16([100; -200; 300; -400]), int16([1; 2; 3; 4])));
            tc.verifyClass(n, 'double');
            tc.verifyEqual(n, 4);
            blk.enqueue(n * bs.sample_size);
            bs.start();
            tc.verifyTrue(blk.dequeue());
            tc.verifyEqual(tc.Fx.readInt16('emu-tx_buf0.bin'), int16([100; 1; -200; 2; 300; 3; -400; 4]));
        end

        function writeGainOffset(tc)
            % the block gets (DATA - Offset) / Gain, per column: the inverse of read
            bs = openTx(tc);
            blk = bs.create_block(2 * bs.sample_size);
            n = blk.write([100 100; 200 200], Gain = [2 4], Offset = [10 -20]);
            tc.verifyEqual(n, 2);
            tc.verifyEqual(blk.read(OutputDataType = "int16"), int16([45 30; 95 55]));
            tc.verifyEqual(blk.read(Gain = [2 4], Offset = [10 -20]), [100 100; 200 200]);
            % complex: the offset is taken from the real and from the imaginary part
            n = blk.write([100 + 50i; 10 + 20i], Gain = 2, Offset = 10);
            tc.verifyEqual(n, 2);
            tc.verifyEqual(blk.read(OutputDataType = "int16"), int16([45 20; 0 5]));
            tc.verifyEqual(blk.read(Complex = true, Gain = 2, Offset = 10), [100 + 50i; 10 + 20i]);
            blk.enqueue(n * bs.sample_size);
            bs.start();
            tc.verifyTrue(blk.dequeue());
            tc.verifyEqual(tc.Fx.readInt16('emu-tx_buf0.bin'), int16([45; 20; 0; 5]));
        end

        function writeShapes(tc)
            bs = openTx(tc);
            blk = bs.create_block(4 * bs.sample_size);
            % more rows than the block holds: the block's share
            tc.verifyEqual(blk.write(complex(int16((1:6).'), int16((11:16).'))), 4);
            tc.verifyEqual(blk.bytes(0, 4), uint8([1; 0; 11; 0]));
            tc.verifyEqual(blk.write(int16(zeros(0, 2))), 0);
            % real data without Complex: one column per channel
            tc.verifyEqual(blk.write(int16([1 2; 3 4])), 2);
            tc.verifyEqual(blk.bytes(0, 8), uint8([1; 0; 2; 0; 3; 0; 4; 0]));
            tc.verifyError(@() blk.write(int16([7; -8; 9; -10])), 'iio:EINVAL');
            tc.verifyError(@() blk.write('abcd'), 'iio:EINVAL');
            tc.verifyError(@() blk.write(complex(int16([1; 2]), int16([3; 4])), Complex = false), 'iio:EINVAL');
            tc.verifyError(@() blk.write(int16([1 2; 3 4]), Scale = "fullscale"), 'iio:EINVAL');
        end

        function writeRealAsComplex(tc)
            % the option, not the complexity of the array as the MEX file sees it, selects the layout
            bs = openTx(tc);
            blk = bs.create_block(4 * bs.sample_size);
            want = int16([7; 0; -8; 0; 9; 0; -10; 0]);
            tc.verifyEqual(blk.write(complex(int16([1; 2; 3; 4]), int16([5; 6; 7; 8]))), 4);
            tc.verifyEqual(blk.write(int16([7; -8; 9; -10]), Complex = true), 4);
            blk.enqueue(16);
            bs.start();
            tc.verifyTrue(blk.dequeue());
            tc.verifyEqual(tc.Fx.readInt16('emu-tx_buf0.bin'), want);
            tc.verifyEqual(blk.write(complex(int16([1; 2; 3; 4]), int16([5; 6; 7; 8]))), 4);
            tc.verifyEqual(blk.write(complex(int16([7; -8; 9; -10]), int16(0))), 4);
            blk.enqueue(16);
            tc.verifyTrue(blk.dequeue());
            tc.verifyEqual(tc.Fx.readInt16('emu-tx_buf0.bin'), want);
        end

        function channelWrite(tc)
            [bs, v0i] = openTx(tc);
            blk = bs.create_block(16);
            tc.verifyEqual(blk.set_bytes(zeros(1, 16, 'uint8')), 16);
            tc.verifyEqual(v0i.write(blk, int16([7 8 9 10])), 8);
            blk.enqueue(16);
            bs.start();
            tc.verifyTrue(blk.dequeue());
            f = tc.Fx.readInt16('emu-tx_buf0.bin');
            tc.verifySize(f, [8 1]);
            tc.verifyEqual(f(1:2:end), int16([7; 8; 9; 10]));
            tc.verifyEqual(f(2:2:end), int16([0; 0; 0; 0]));
        end

        function setBytes(tc)
            blk = rxBlock(tc);
            n = blk.set_bytes(uint8(1:4), 12);
            tc.verifyEqual(n, 4);
            tc.verifyEqual(blk.bytes(12, 4), uint8((1:4).'));
            tc.verifyEqual(blk.bytes(8, 4), uint8([2; 40; 253; 7]));    % the neighbours are untouched
            tc.verifyEqual(blk.set_bytes(uint8([9 9])), 2);
            tc.verifyEqual(blk.bytes(0, 4), uint8([9; 9; 255; 7]));
            tc.verifyError(@() blk.set_bytes(uint8(1:4), 30), 'iio:EINVAL');
            tc.verifyError(@() blk.set_bytes([1 2 3]), 'iio:EINVAL');   % not uint8
        end

        function parent(tc)
            [blk, bs] = rxBlock(tc);
            tc.verifyClass(blk.parent, 'iio.BufferStream');
            tc.verifyTrue(blk.parent == bs);
            tc.verifyClass(blk.buffer_stream, 'iio.BufferStream');
            tc.verifyTrue(blk.buffer_stream == bs);
        end

        function dmabuf(tc)
            blk = rxBlock(tc);
            tc.verifyError(@() blk.dmabuf_fd(), 'iio:EINVAL');
        end

        function cpuAccess(tc)
            blk = rxBlock(tc);
            tc.verifyError(@() blk.disable_cpu_access(true), 'iio:ENOSYS');
            tc.verifySize(blk.read(), [8 2]);       % still readable
        end

        function destroyTwice(tc)
            [blk, bs] = rxBlock(tc);
            blk.destroy();
            blk.destroy();
            tc.verifyTrue(isvalid(blk));
            tc.verifyError(@() blk.nbytes, 'iio:EBADF');
            tc.verifyError(@() blk.read(), 'iio:EBADF');
            % the buffer stream is untouched: it makes another block
            other = bs.create_block(8 * bs.sample_size);
            tc.verifyEqual(other.nbytes, 32);
        end

        function blockOutlivesItsStream(tc)
            [blk, bs] = rxBlock(tc);
            bs.close();
            tc.verifyError(@() blk.read(), 'iio:EBADF');
            delete(blk);    % a stale handle: nothing happens
            tc.verifyFalse(isvalid(blk));
        end
    end
end

function [bs, v0i, v0q] = openRx(tc)
%OPENRX Buffer stream of emu-rx with voltage0_i and voltage0_q enabled (4 bytes per sample).
ctx = iio.Context(tc.Fx.Uri);
dev = ctx.find_device("emu-rx");
v0i = dev.find_channel("voltage0_i");
v0q = dev.find_channel("voltage0_q");
mask = iio.ChannelsMask(dev);
enable([v0i v0q], mask);
bufs = dev.buffers;
bs = bufs(1).open(mask);
end

function [blk, bs, v0i, v0q] = rxBlock(tc)
%RXBLOCK A block of 8 samples of openRx, filled: enqueued, the stream started, dequeued.
[bs, v0i, v0q] = openRx(tc);
blk = bs.create_block(8 * bs.sample_size);
blk.enqueue();
bs.start();
blk.dequeue();
end

function [bs, v0i, v0q] = openTx(tc)
%OPENTX Buffer stream of emu-tx with the output channels voltage0_i and voltage0_q enabled.
ctx = iio.Context(tc.Fx.Uri);
dev = ctx.find_device("emu-tx");
v0i = dev.find_channel("voltage0_i", true);
v0q = dev.find_channel("voltage0_q", true);
mask = iio.ChannelsMask(dev);
enable([v0i v0q], mask);
bufs = dev.buffers;
bs = bufs(1).open(mask);
end
