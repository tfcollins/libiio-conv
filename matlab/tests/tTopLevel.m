classdef tTopLevel < IioTestCase
    %TTOPLEVEL Package functions: library version, backends, error text, context scan.

    methods (Test)
        function version(tc)
            v = iio.version();
            tc.verifyClass(v, 'struct');
            tc.verifySize(v, [1 1]);
            tc.verifyEqual(fieldnames(v), {'major'; 'minor'; 'patch'; 'tag'});
            tc.verifyEqual(v.major, 1);
            tc.verifyClass(v.minor, 'double');
            tc.verifySize(v.minor, [1 1]);
            tc.verifyGreaterThanOrEqual(v.minor, 0);
            tc.verifyClass(v.patch, 'double');
            tc.verifySize(v.patch, [1 1]);
            tc.verifyGreaterThanOrEqual(v.patch, 0);
            tc.verifyClass(v.tag, 'char');
            tc.verifyFalse(isempty(v.tag));
            tc.verifyEqual(size(v.tag, 1), 1);
            % the library's own version, not the one a context reports for its remote side
            ctx = iio.Context(tc.Fx.Uri);
            tc.verifyEqual(ctx.version.tag, 'v1.2.3-fixture');
            tc.verifyNotEqual(v.tag, 'v1.2.3-fixture');
            tc.verifyEqual(iio.version(), v);
        end

        function backends(tc)
            b = iio.backends();
            tc.verifyClass(b, 'cell');
            tc.verifySize(b, [1 4]);
            tc.verifyEqual(b, {'local', 'ip', 'emu', 'xml'});
        end

        function hasBackend(tc)
            tc.verifyEqual(iio.has_backend("emu"), true);
            tc.verifyEqual(iio.has_backend('xml'), true);
            tc.verifyEqual(iio.has_backend("usb"), false);
            tc.verifyEqual(iio.has_backend(""), false);
            b = iio.backends();
            for k = 1:numel(b)
                tc.verifyTrue(iio.has_backend(b{k}), b{k});
            end
        end

        function strerror(tc)
            s = iio.strerror(-22);
            tc.verifyClass(s, 'char');
            tc.verifyEqual(s, 'Invalid argument (-22)');
            tc.verifyEqual(iio.strerror(0), 'Success (0)');
            tc.verifyEqual(iio.strerror(int32(-22)), 'Invalid argument (-22)');
            tc.verifyError(@() iio.strerror(2^31), 'iio:EINVAL');
            % argument validation: an identifier of MATLAB itself, whatever its exact name
            id = '';
            try
                iio.strerror(1.5);
            catch err
                id = err.identifier;
            end
            tc.verifyTrue(startsWith(id, 'MATLAB:'), ['identifier: ' id]);
        end

        function scanEmpty(tc)
            % the emu backend cannot scan: no result, and the scan itself is released
            r = iio.scan_contexts("emu", LogLevel = "nolog");
            tc.verifyClass(r, 'struct');
            tc.verifyEqual(size(r), [1 0]);
            tc.verifyEqual(fieldnames(r), {'uri'; 'description'});
            tc.verifyEqual(iio.internal.handle_count(), 0);
        end

        function scanArguments(tc)
            tc.verifyError(@() iio.scan_contexts("emu", Timeout = 1.5), 'iio:EINVAL');
            % argument validation: an identifier of MATLAB itself, whatever its exact name
            id = '';
            try
                iio.scan_contexts("emu", LogLevel = "loud");
            catch err
                id = err.identifier;
            end
            tc.verifyTrue(startsWith(id, 'MATLAB:'), ['identifier: ' id]);
            r = iio.scan_contexts('emu', LogLevel = 'nolog');
            tc.verifyClass(r, 'struct');
            tc.verifyEqual(size(r), [1 0]);
            tc.verifyEqual(fieldnames(r), {'uri'; 'description'});
            r = iio.scan_contexts("emu", Timeout = Inf, LogLevel = "nolog");
            tc.verifyEqual(size(r), [1 0]);
            tc.verifyEqual(iio.internal.handle_count(), 0);
        end
    end
end
