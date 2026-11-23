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

        function scanEmptyBackendsIsDefault(tc)
            % "" is no list of backends: libiio's default list, as with no argument at all.
            % How many contexts the default list finds depends on the machine (the local
            % backend); the two calls agree on it. An empty C string instead would be a
            % list of no backend and find nothing on a machine that has a context.
            % On a machine without one this does not tell the two apart:
            % scanEmptyBackendsSendsNoList does, by what the gateway is given.
            d = iio.scan_contexts(LogLevel = "nolog");
            r = iio.scan_contexts("", LogLevel = "nolog");
            tc.verifyClass(r, 'struct');
            tc.verifyEqual(size(r, 1), 1);
            tc.verifyEqual(fieldnames(r), {'uri'; 'description'});
            tc.verifyEqual(r, d);
            tc.verifyEqual(iio.scan_contexts(''), iio.scan_contexts());
            tc.verifyEqual(iio.scan_contexts("", Timeout = 1000, LogLevel = "nolog"), d);
            tc.verifyEqual(iio.internal.handle_count(), 0);
        end

        function scanEmptyBackendsSendsNoList(tc)
            % What iio.scan_contexts hands to the gateway. The result of a scan cannot
            % tell "" sent as [] from "" sent as '' on a machine without a local context
            % (both find nothing), so an M function named iio_mex, in a folder put in
            % front on the path, stands in for the MEX file and records its arguments.
            d = tempname;
            mkdir(d);
            rmDir = onCleanup(@() rmdir(d, 's'));
            writelines([ ...
                "function varargout = iio_mex(cmd, varargin)"
                "persistent calls"
                "switch cmd"
                "    case 'recorded'"
                "        varargout{1} = calls;"
                "    case 'scan'"
                "        calls{end + 1} = varargin;"
                "        varargout{1} = uint64(1);"
                "    case 'scan_destroy'"
                "    case 'scan_get_results'"
                "        varargout = {cell(1, 0), cell(1, 0)};"
                "    otherwise"
                "        error('iio:ENOSYS', 'the stand-in has no command %s', cmd);"
                "end"
                "end"], fullfile(d, 'iio_mex.m'));
            before = addpath(d);
            restore = onCleanup(@() path(before));
            tc.assertEqual(which('iio_mex'), fullfile(d, 'iio_mex.m'));

            iio.scan_contexts();
            iio.scan_contexts("");
            iio.scan_contexts('');
            iio.scan_contexts("", Timeout = 1000);
            iio.scan_contexts(LogLevel = "error");
            r = iio.scan_contexts("usb,ip");
            % feval: 'recorded' is a command of the stand-in, not of the gateway (check_api.py
            % holds every direct call to the gateway's command list)
            calls = feval('iio_mex', 'recorded');
            clear restore       % the MEX file again, before anything else is verified
            tc.assertNotEqual(which('iio_mex'), fullfile(d, 'iio_mex.m'));

            tc.verifyEqual(size(r), [1 0]);
            tc.verifyEqual(fieldnames(r), {'uri'; 'description'});
            tc.assertSize(calls, [1 6]);
            % [] (a 0-by-0 double) is C NULL, libiio's default list; '' would be a C
            % string that names no backend
            tc.verifyEqual(calls{1}, {[], []});
            tc.verifyEqual(calls{2}, {[], []});
            tc.verifyEqual(calls{3}, {[], []});
            tc.verifyEqual(calls{4}, {struct('timeout_ms', iio.internal.timeout_ms(1000)), []});
            tc.verifyEqual(calls{5}, {struct('log_level', 2), []});
            tc.verifyEqual(calls{6}, {[], 'usb,ip'});
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
