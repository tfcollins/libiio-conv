classdef tLifetime < IioTestCase
    %TLIFETIME iio objects that outlive their context, go out of scope in any order,
    %   survive "clear mex", or were never completely built.
    %   A destroy on an object whose context is gone does nothing; any other use of it
    %   raises iio:EBADF. An error inside a delete method would only be a warning, so
    %   these tests also check that no warning is issued.

    methods (Test)
        function childAfterContextDelete(tc)
            ctx = iio.Context(tc.Fx.Uri);
            phy = ctx.find_device("emu-phy");
            a = phy.find_attr("text");
            ch = phy.find_channel("voltage0");
            [mask, bs, blk, r] = dataPathObjects(ctx);
            tc.verifyEqual(a.read(), 'hello world');
            tc.verifyGreaterThan(iio.internal.handle_count(), 6);

            delete(ctx);
            tc.verifyFalse(isvalid(ctx));
            tc.verifyEqual(iio.internal.handle_count(), 0);
            % the objects are still there, what they stood for is not
            tc.verifyTrue(isvalid(a));
            tc.verifyEqual(a.name, 'text');
            tc.verifyError(@() a.read(), 'iio:EBADF');
            tc.verifyError(@() a.write("x"), 'iio:EBADF');
            tc.verifyError(@() phy.find_attr("text"), 'iio:EBADF');
            tc.verifyError(@() ch.find_attr("hardwaregain"), 'iio:EBADF');
            tc.verifyError(@() bs.start(), 'iio:EBADF');
            tc.verifyError(@() blk.enqueue(), 'iio:EBADF');
            tc.verifyError(@() r.read(), 'iio:EBADF');
            % deleting them afterwards, in any order, raises nothing and warns of nothing
            tc.verifyWarningFree(@() delete(mask));
            tc.verifyWarningFree(@() delete(r));
            tc.verifyWarningFree(@() delete(bs));
            tc.verifyWarningFree(@() delete(blk));
            tc.verifyWarningFree(@() delete(a));
            tc.verifyWarningFree(@() delete(ch));
            tc.verifyWarningFree(@() delete(phy));
            tc.verifyFalse(isvalid(blk));
            tc.verifyEqual(iio.internal.handle_count(), 0);
        end

        function scopeExitAnyOrder(tc)
            % every object is a local of buildAndDrop: MATLAB clears them in an order of
            % its own when the function returns
            tc.verifyWarningFree(@() buildAndDrop(tc.Fx.Uri));
            tc.verifyEqual(iio.internal.handle_count(), 0);
            for k = 1:10
                buildAndDrop(tc.Fx.Uri);
            end
            tc.verifyEqual(iio.internal.handle_count(), 0);
        end

        function clearMexKeepsObjects(tc)
            ctx = iio.Context(tc.Fx.Uri);
            a = ctx.find_device("emu-phy").find_attr("text");
            n = iio.internal.handle_count();
            clear mex %#ok<CLMEX>
            % the gateway holds handles: it is locked and was not unloaded
            [~, loaded] = inmem;
            tc.verifyTrue(ismember('iio_mex', loaded));
            tc.verifyEqual(iio.internal.handle_count(), n);
            tc.verifyEqual(a.read(), 'hello world');
            tc.verifyEqual(ctx.name, 'emu');
            tc.verifyEqual(ctx.find_attr("hw_model").read(), 'Emulated Test Board');
        end

        function shutdownClosesStreams(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            r = iio.RxStream(rx, ["voltage0_i" "voltage0_q"], SamplesPerFrame = 16, ...
                OutputDataType = "int16", Complex = true);
            [d, valid] = r.read();
            tc.verifyEqual(valid, true);
            tc.verifyEqual(d(1), complex(int16(-2048), int16(2047)));

            iio.internal.shutdown();
            tc.verifyEqual(iio.internal.handle_count(), 0);
            tc.verifyError(@() r.read(), 'iio:EBADF');
            tc.verifyError(@() r.flush(), 'iio:EBADF');
            tc.verifyError(@() ctx.find_device("emu-rx"), 'iio:EBADF');

            % the gateway works on: a new context, and a new stream from its first frame
            again = iio.Context(tc.Fx.Uri);
            tc.verifyEqual(again.find_attr("hw_model").read(), 'Emulated Test Board');
            r2 = iio.RxStream(again.find_device("emu-rx"), ["voltage0_i" "voltage0_q"], ...
                SamplesPerFrame = 16, OutputDataType = "int16", Complex = true);
            [d, valid] = r2.read();
            tc.verifyEqual(valid, true);
            tc.verifyEqual(d(1), complex(int16(-2048), int16(2047)));
            % the objects of before the shutdown are closed while the new ones live
            r.close();
            delete(r);
            delete(ctx);
            tc.verifyEqual(again.name, 'emu');
            [d, valid] = r2.read();
            tc.verifyEqual(valid, true);
            tc.verifyEqual(d(1), complex(int16(-2032), int16(2031)));
        end

        function constructorFailureLeavesNoHandle(tc)
            ctx = iio.Context(tc.Fx.Uri);
            rx = ctx.find_device("emu-rx");
            % an unknown channel: the open fails before a buffer is chosen
            tc.verifyError(@() iio.RxStream(rx, "nope", SamplesPerFrame = 16), 'iio:ENOENT');
            n = iio.internal.handle_count();
            for k = 1:50
                tc.verifyError(@() iio.RxStream(rx, "nope", SamplesPerFrame = 16), 'iio:ENOENT');
            end
            tc.verifyEqual(iio.internal.handle_count(), n);
            % Gain = 0: the open fails after the buffer was chosen
            tc.verifyError(@() iio.RxStream(rx, ["voltage0_i" "voltage0_q"], SamplesPerFrame = 16, Gain = 0), 'iio:EINVAL');
            n = iio.internal.handle_count();
            for k = 1:50
                tc.verifyError(@() iio.RxStream(rx, ["voltage0_i" "voltage0_q"], SamplesPerFrame = 16, Gain = 0), 'iio:EINVAL');
            end
            tc.verifyEqual(iio.internal.handle_count(), n);
            % the same for a transmit stream
            tx = ctx.find_device("emu-tx");
            tc.verifyError(@() iio.TxStream(tx, "nope", SamplesPerFrame = 16), 'iio:ENOENT');
            n = iio.internal.handle_count();
            for k = 1:50
                tc.verifyError(@() iio.TxStream(tx, "nope", SamplesPerFrame = 16), 'iio:ENOENT');
            end
            tc.verifyEqual(iio.internal.handle_count(), n);
            % and the device still streams
            r = iio.RxStream(rx, ["voltage0_i" "voltage0_q"], SamplesPerFrame = 16);
            [d, valid] = r.read();
            tc.verifyEqual(valid, true);
            tc.verifyEqual(d(1, :), [-2048 2047]);
            delete(r);
            delete(ctx);
            tc.verifyEqual(iio.internal.handle_count(), 0);
        end

        function errorInDeleteIsNotSwallowed(tc)
            % MATLAB turns an error inside delete into a warning. Deleting in the wrong
            % order must therefore not raise inside delete at all.
            ctx = iio.Context(tc.Fx.Uri);
            [mask, bs, blk, r] = dataPathObjects(ctx); %#ok<ASGLU> cleared below, one by one
            a = ctx.find_attr("hw_model");
            tc.verifyEqual(a.read(), 'Emulated Test Board');
            [oldMsg, oldId] = lastwarn;
            restore = onCleanup(@() lastwarn(oldMsg, oldId));
            lastwarn('none so far', 'tLifetime:none');
            delete(ctx);
            clear r
            clear blk
            clear bs
            clear mask
            clear a
            [msg, id] = lastwarn;
            tc.verifyEqual(id, 'tLifetime:none');
            tc.verifyEqual(msg, 'none so far');
            tc.verifyEqual(iio.internal.handle_count(), 0);
        end
    end
end

function [mask, bs, blk, r] = dataPathObjects(ctx)
%DATAPATHOBJECTS One object of every owning class on CTX: a mask, a buffer stream and a
%   block on emu-tx, and a started receive stream on emu-rx.
tx = ctx.find_device("emu-tx");
mask = iio.ChannelsMask(tx);
enable([tx.find_channel("voltage0_i", true) tx.find_channel("voltage0_q", true)], mask);
bufs = tx.buffers;
bs = bufs(1).open(mask);
blk = bs.create_block(16);
r = iio.RxStream(ctx.find_device("emu-rx"), ["voltage0_i" "voltage0_q"], SamplesPerFrame = 16, ...
    OutputDataType = "int16", Complex = true);
end

function buildAndDrop(uri)
%BUILDANDDROP Context, device, mask, buffer stream, block and receive stream as locals.
ctx = iio.Context(uri);
[mask, bs, blk, r] = dataPathObjects(ctx); %#ok<ASGLU>
d = r.read();
assert(isequal(size(d), [16 1]));
end
