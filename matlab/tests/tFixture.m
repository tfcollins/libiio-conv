classdef tFixture < IioTestCase
    %TFIXTURE The test fixture itself: ramp data, files, converters, the leak check.

    methods (Test)
        function rampAnchors(tc)
            w = EmuFixture.rampWords(4096);
            tc.verifyClass(w, 'uint16');
            tc.verifySize(w, [1 8192]);
            tc.verifyEqual(w(1:4), uint16([2048 2047 6145 2046]));
            tc.verifyEqual(w(65:66), uint16([2064 6127]));       % W(32), W(33) = 0810 17ef
            tc.verifyEqual(w(end-1:end), uint16([63487 63488]));
            tc.verifyEqual(sum(double(w)), 268431360);
            w = EmuFixture.rampWords(4097);
            tc.verifySize(w, [1 8194]);
            tc.verifyEqual(w(8193), uint16(2048));
        end

        function defaultFile(tc)
            name = fullfile(tc.Fx.Dir, 'emu-rx_buf0.bin');
            d = dir(name);
            tc.verifySize(d, [1 1]);
            tc.verifyEqual(d.bytes, 16384);
            fid = fopen(name, 'r');
            tc.assertGreaterThanOrEqual(fid, 3);
            b = fread(fid, 4);
            fclose(fid);
            tc.verifyEqual(b.', [0 8 255 7]);
            tc.verifyTrue(isfile(tc.Fx.XmlFile));
            tc.verifyEqual(tc.Fx.Uri, ['emu:' tc.Fx.XmlFile]);
            tc.verifyEqual(tc.Fx.XmlUri, ['xml:' tc.Fx.XmlFile]);
        end

        function converters(tc)
            tc.verifyEqual(EmuFixture.s12(uint16([2048 2047 6145 2046])), int16([-2048 2047 -2047 2046]));
            tc.verifyEqual(EmuFixture.s14shift2(uint16([2048 2047 6145])), int16([512 511 1536]));
            tc.verifyEqual(EmuFixture.s14shift2(uint16([65535; 32768])), int16([-1; -8192]));
        end

        function readInt16RoundTrip(tc)
            tc.Fx.writeWords('x.bin', uint16([1 65535 32768]));
            tc.verifyEqual(tc.Fx.readInt16('x.bin'), int16([1; -1; -32768]));
            tc.verifyError(@() tc.Fx.readInt16('missing.bin'), 'EmuFixture:open');
        end

        function leakCheckIsLive(tc)
            tc.verifyEqual(iio.internal.handle_count(), 0);
            ctx = iio.Context(tc.Fx.Uri);
            tc.verifyEqual(iio.internal.handle_count(), 1);
            delete(ctx);
            tc.verifyEqual(iio.internal.handle_count(), 0);
        end

        function verifyIioErrorWorks(tc)
            tc.verifyIioError(@() iio.Context("bogus:foo"), "ENOSYS");
        end
    end
end
