classdef tToolboxFlow < IioTestCase
    %TTOOLBOXFLOW The flow of a transceiver toolbox object, end to end on the emu fixture.
    %   MiniRx and MiniTx do what a toolbox receive or transmit object does, in the same
    %   order: context, timeout, device lookup, attribute writes that are read back,
    %   stream creation, step, release. The control path is tested on emu-phy, the data
    %   path on emu-rx (the ramp of EmuFixture) and emu-tx (emu-tx_buf0.bin).
    %   Ramp sample k (taken modulo 4096) is I(k) = k - 2048, Q(k) = 2047 - k.

    methods (Test)
        function controlPath(tc)
            % typed attribute I/O by name, on a device that has no buffer
            ctx = iio.Context(tc.Fx.Uri);
            phy = ctx.find_device("emu-phy");
            tc.verifySize(phy, [1 1]);
            tc.verifyEmpty(phy.buffers);
            rx1 = phy.find_channel("voltage0");
            bw = rx1.find_attr("rf_bandwidth");
            bw.write(int64(20000000));
            tc.verifyEqual(bw.read(), '20000000');
            % below the range of rf_bandwidth_available: refused, and nothing changed
            tc.verifyError(@() bw.write(100), 'iio:EINVAL');
            tc.verifyEqual(bw.read(), '20000000');
            gcm = rx1.find_attr("gain_control_mode");
            gcm.write("manual");
            tc.verifyEqual(gcm.read(), 'manual');
            tc.verifyError(@() gcm.write("bogus"), 'iio:EINVAL');
            tc.verifyEqual(gcm.read(), 'manual');
            % a frequency above 2^31
            lo = phy.find_channel("altvoltage0", true);
            freq = lo.find_attr("frequency");
            freq.write(int64(2450000000));
            v = freq.read_longlong();
            tc.verifyClass(v, 'int64');
            tc.verifyEqual(v, int64(2450000000));
            % a debug attribute
            lb = phy.find_debug_attr("loopback");
            lb.write(1);
            tc.verifyEqual(lb.read(), '1');
            % a register
            phy.reg_write(16, 51966);
            tc.verifyEqual(phy.reg_read(16), uint32(51966));
            % a device that is not there is an empty result, not an error
            none = ctx.find_device("hmc425a");
            tc.verifyClass(none, 'iio.Device');
            tc.verifySize(none, [1 0]);
        end

        function rxParityRaw(tc)
            % what a toolbox receiver delivers: int16, complex, the storage words as they are
            w = EmuFixture.rampWords(4096);
            wi = w(1:2:end);
            wq = w(2:2:end);
            rx = MiniRx(tc.Fx.Uri, Channels = ["voltage0_i", "voltage0_q"], SamplesPerFrame = 1000, ...
                OutputDataType = "int16", Complex = true, Raw = true);
            rx.setup();
            for f = 0:5
                [d, v] = rx.step();
                msg = sprintf('frame %d', f);
                tc.verifyClass(v, 'logical', msg);
                tc.verifyEqual(v, true, msg);
                tc.verifyClass(d, 'int16', msg);
                tc.verifySize(d, [1000 1], msg);
                tc.verifyFalse(isreal(d), msg);
                k = mod(f * 1000 + (0:999), 4096);
                tc.verifyEqual(d, complex(typecast(wi(k + 1), 'int16'), typecast(wq(k + 1), 'int16')).', msg);
            end
            rx.release();
        end

        function rxConverted(tc)
            % the same without Raw: the 12 bits of each word, sign-extended
            rx = MiniRx(tc.Fx.Uri, Channels = ["voltage0_i", "voltage0_q"], SamplesPerFrame = 1000, ...
                OutputDataType = "int16", Complex = true, Raw = false);
            rx.setup();
            [d, v] = rx.step();
            tc.verifyEqual(v, true);
            tc.verifyClass(d, 'int16');
            tc.verifySize(d, [1000 1]);
            tc.verifyEqual(d, rampFrame(0:999));
            tc.verifyEqual(d(1:2), complex(int16([-2048; -2047]), int16([2047; 2046])));
            rx.release();
        end

        function rxTwoComplexColumnsDouble(tc)
            % four ids are two complex columns; with both pairs enabled sample s holds ramp
            % sample 2s for voltage0 and 2s + 1 for voltage1
            rx = MiniRx(tc.Fx.Uri, Channels = ["voltage0_i", "voltage0_q", "voltage1_i", "voltage1_q"], ...
                SamplesPerFrame = 1000, OutputDataType = "double", Complex = true, Raw = false, ...
                Scale = "fullscale");
            rx.setup();
            [d, v] = rx.step();
            tc.verifyEqual(v, true);
            tc.verifyClass(d, 'double');
            tc.verifySize(d, [1000 2]);
            tc.verifyFalse(isreal(d));
            s = (0:999).';
            k0 = 2 * s;
            k1 = 2 * s + 1;
            tc.verifyEqual(d(:, 1), complex(k0 - 2048, 2047 - k0) / 2048);
            tc.verifyEqual(d(:, 2), complex(k1 - 2048, 2047 - k1) / 2048);
            tc.verifyEqual(d(1, :), [-1 + 0.99951171875i, -0.99951171875 + 0.9990234375i]);
            rx.release();
        end

        function rxPairingFollowsIdOrder(tc)
            % Q before I in the list: the real part is Q, the imaginary part I
            rx = MiniRx(tc.Fx.Uri, Channels = ["voltage0_q", "voltage0_i"], SamplesPerFrame = 1000, ...
                OutputDataType = "int16", Complex = true, Raw = false);
            rx.setup();
            [d, v] = rx.step();
            tc.verifyEqual(v, true);
            k = (0:999).';
            tc.verifyEqual(d, complex(int16(2047 - k), int16(k - 2048)));
            rx.release();
        end

        function rxRealIioScale(tc)
            % one real channel in its unit: (bits 15..2 of each word - 100) * 0.25
            w = EmuFixture.rampWords(4096);
            rx = MiniRx(tc.Fx.Uri, Channels = "voltage2", SamplesPerFrame = 1000, ...
                OutputDataType = "single", Complex = false, Raw = false, Scale = "iio");
            rx.setup();
            [d, v] = rx.step();
            tc.verifyEqual(v, true);
            tc.verifyClass(d, 'single');
            tc.verifySize(d, [1000 1]);
            tc.verifyTrue(isreal(d));
            tc.verifyEqual(d, single((double(EmuFixture.s14shift2(w(1:1000))).' - 100) * 0.25));
            tc.verifyEqual(d(1:6), single([103; 102.75; 359; 102.75; 615; 102.75]));
            rx.release();
        end

        function rxSurvivesWrap(tc)
            % frame 4 holds samples 4000 to 4999: the ramp starts again at its row 97
            rx = MiniRx(tc.Fx.Uri, Channels = ["voltage0_i", "voltage0_q"], SamplesPerFrame = 1000, ...
                OutputDataType = "int16", Complex = true, Raw = false);
            rx.setup();
            for f = 0:3
                [~, v] = rx.step();
                tc.verifyEqual(v, true, sprintf('frame %d', f));
            end
            [d, v] = rx.step();
            tc.verifyEqual(v, true);
            tc.verifySize(d, [1000 1]);
            tc.verifyEqual(d, rampFrame(4000 + (0:999)));
            tc.verifyEqual(d(96:97), complex(int16([2047; -2048]), int16([-2048; 2047])));
            rx.release();
        end

        function rxReleaseAndReconfigure(tc)
            % release, another frame size, setup: the first frame again, and nothing is left
            rx = MiniRx(tc.Fx.Uri, Channels = ["voltage0_i", "voltage0_q"], SamplesPerFrame = 1000, ...
                OutputDataType = "int16", Complex = true, Raw = false);
            rx.setup();
            d = rx.step();
            tc.verifyEqual(d, rampFrame(0:999));
            d = rx.step();
            tc.verifyEqual(d, rampFrame(1000 + (0:999)));
            rx.release();
            tc.verifyEqual(iio.internal.handle_count(), 0);
            tc.verifyError(@() rx.step(), 'MiniRx:notSetUp');
            rx.SamplesPerFrame = 256;
            rx.setup();
            [d, v] = rx.step();
            tc.verifyEqual(v, true);
            tc.verifySize(d, [256 1]);
            tc.verifyEqual(d, rampFrame(0:255));
            rx.release();
            tc.verifyEqual(iio.internal.handle_count(), 0);
            tc.verifyWarningFree(@() rx.release());
            tc.verifyEqual(iio.internal.handle_count(), 0);
        end

        function setupFailureReleases(tc)
            % a setup that fails at any stage leaves no handle, and release stays harmless
            rx = MiniRx(tc.Fx.Uri);
            tc.verifyWarningFree(@() rx.release());         % never set up
            rx.Device = "hmc425a";                          % no such device
            tc.verifyError(@() rx.setup(), 'MiniRx:noDevice');
            tc.verifyEqual(iio.internal.handle_count(), 0);
            rx.Device = "emu-rx";
            rx.Channels = ["voltage0_i", "voltage9"];       % fails when the stream opens
            tc.verifyError(@() rx.setup(), 'iio:ENOENT');
            tc.verifyEqual(iio.internal.handle_count(), 0);
            tc.verifyWarningFree(@() rx.release());
            % and the object is still usable
            rx.Channels = ["voltage0_i", "voltage0_q"];
            rx.SamplesPerFrame = 256;
            rx.setup();
            [d, v] = rx.step();
            tc.verifyEqual(v, true);
            tc.verifyEqual(d, rampFrame(0:255));
            rx.release();
            tx = MiniTx(tc.Fx.Uri);
            tc.verifyWarningFree(@() tx.release());         % never set up
            tx.Device = "emu-rx";                           % a device without the tone channel
            tc.verifyError(@() tx.setup(), 'MiniTx:noChannel');
            tc.verifyEqual(iio.internal.handle_count(), 0);
            tc.verifyError(@() tx.step(txFrame(0)), 'MiniTx:notSetUp');
            tc.verifyWarningFree(@() tx.release());
        end

        function txComplexInt16(tc)
            tx = MiniTx(tc.Fx.Uri, Channels = ["voltage0_i", "voltage0_q"], SamplesPerFrame = 256);
            tx.setup();
            a = txFrame(0);
            v = tx.step(a);
            tc.verifyClass(v, 'logical');
            tc.verifyEqual(v, true);
            tx.flush();
            got = tc.Fx.readInt16('emu-tx_buf0.bin');
            tc.verifySize(got, [512 1]);
            tc.verifyEqual(got, reshape([real(a) imag(a)].', [], 1));
            tc.verifyEqual(got(1:4), int16([100; -100; 101; -101]));
            tx.release();
        end

        function txDoubleRoundsAndSaturates(tc)
            % halves round to the even integer, values beyond int16 saturate, NaN is sent as 0
            tx = MiniTx(tc.Fx.Uri, SamplesPerFrame = 9);
            tx.setup();
            v = tx.step(complex([0.4 0.5 1.5 2.5 -0.5 -1.5 40000 -40000 NaN].', 0));
            tc.verifyEqual(v, true);
            tx.flush();
            got = tc.Fx.readInt16('emu-tx_buf0.bin');
            tc.verifySize(got, [18 1]);
            tc.verifyEqual(got(1:2:end), int16([0 0 2 2 0 -2 32767 -32768 0]).');
            tc.verifyEqual(got(2:2:end), zeros(9, 1, 'int16'));
            tx.release();
        end

        function txRealInputToComplexStream(tc)
            % a complex array whose imaginary part is all zero arrives real: it is accepted
            tx = MiniTx(tc.Fx.Uri, SamplesPerFrame = 256);
            tx.setup();
            v = tx.step(double((1:256).'));
            tc.verifyEqual(v, true);
            tx.flush();
            got = tc.Fx.readInt16('emu-tx_buf0.bin');
            tc.verifySize(got, [512 1]);
            tc.verifyEqual(got(1:2:end), int16((1:256).'));
            tc.verifyEqual(got(2:2:end), zeros(256, 1, 'int16'));
            tx.release();
        end

        function txLastFrameOnRelease(tc)
            % no flush: the release sends what is in flight, and the file is the last frame
            tx = MiniTx(tc.Fx.Uri, SamplesPerFrame = 256);
            tx.NumBlocks = 2;
            tx.setup();
            b = txFrame(1000);
            tc.verifyEqual(tx.step(txFrame(0)), true);
            tc.verifyEqual(tx.step(b), true);
            tx.release();
            tc.verifyEqual(iio.internal.handle_count(), 0);
            got = tc.Fx.readInt16('emu-tx_buf0.bin');
            tc.verifySize(got, [512 1]);
            tc.verifyEqual(got, reshape([real(b) imag(b)].', [], 1));
            tc.verifyEqual(got(1:2), int16([1100; -100]));
        end

        function txWrongShape(tc)
            tx = MiniTx(tc.Fx.Uri, SamplesPerFrame = 256);
            tx.setup();
            % two columns for one I/Q pair
            tc.verifyError(@() tx.step(complex(zeros(256, 2, 'int16'), ones(256, 2, 'int16'))), 'iio:EINVAL');
            tc.verifyError(@() tx.step(zeros(256, 2)), 'iio:EINVAL');
            % one row more than SamplesPerFrame
            tc.verifyError(@() tx.step(complex(zeros(257, 1, 'int16'), ones(257, 1, 'int16'))), 'iio:EINVAL');
            % the rejected calls sent nothing
            a = txFrame(0);
            tc.verifyEqual(tx.step(a), true);
            tx.flush();
            tc.verifyEqual(tc.Fx.readInt16('emu-tx_buf0.bin'), reshape([real(a) imag(a)].', [], 1));
            tx.release();
        end

        function txCyclicSecondWrite(tc)
            tx = MiniTx(tc.Fx.Uri, SamplesPerFrame = 256, Cyclic = true);
            tx.setup();
            tc.verifyEqual(tx.step(txFrame(0)), true);
            tc.verifyError(@() tx.step(txFrame(0)), 'iio:EBUSY');
            tc.verifyWarningFree(@() tx.release());
            tc.verifyEqual(iio.internal.handle_count(), 0);
        end

        function rxAndTxTogether(tc)
            % a receiver and a transmitter on the same URI: two contexts, steps in turn
            rx = MiniRx(tc.Fx.Uri, Channels = ["voltage0_i", "voltage0_q"], SamplesPerFrame = 1000, ...
                OutputDataType = "int16", Complex = true, Raw = false);
            tx = MiniTx(tc.Fx.Uri, Channels = ["voltage0_i", "voltage0_q"], SamplesPerFrame = 256);
            rx.setup();
            tx.setup();
            for f = 0:2
                msg = sprintf('step %d', f);
                [d, v] = rx.step();
                tc.verifyEqual(v, true, msg);
                tc.verifyEqual(d, rampFrame(f * 1000 + (0:999)), msg);
                a = txFrame(1000 * f);
                tc.verifyEqual(tx.step(a), true, msg);
                tx.flush();
                tc.verifyEqual(tc.Fx.readInt16('emu-tx_buf0.bin'), reshape([real(a) imag(a)].', [], 1), msg);
            end
            rx.release();
            tc.verifyGreaterThan(iio.internal.handle_count(), 0);   % the transmitter has its own context
            tx.release();
            tc.verifyEqual(iio.internal.handle_count(), 0);
        end
    end
end

function d = rampFrame(k)
%RAMPFRAME Complex int16 column: the converted ramp samples K (taken modulo 4096).
k = mod(k(:), 4096);
d = complex(int16(k - 2048), int16(2047 - k));
end

function a = txFrame(add)
%TXFRAME 256-by-1 complex int16: I = 100 + ADD + (0:255), Q = -(100 + (0:255)).
n = (0:255).';
a = complex(int16(100 + add + n), int16(-(100 + n)));
end
