classdef tSlice < IioTestCase
    %TSLICE The first slice through the gateway: context, one attribute, unload and reload.

    methods (Test)
        function readContextAttribute(tc)
            ctx = iio.Context(tc.Fx.Uri);
            tc.verifyEqual(ctx.find_attr("hw_model").read(), 'Emulated Test Board');
            none = ctx.find_attr('nope');
            tc.verifyEmpty(none);
            tc.verifyClass(none, 'iio.Attr');
            tc.verifySize(none, [1 0]);
            delete(ctx);
            tc.verifyFalse(isvalid(ctx));
            tc.verifyEqual(iio.internal.handle_count(), 0);
        end

        function errorIdentifiers(tc)
            tc.verifyError(@() iio.Context("bogus:foo"), 'iio:ENOSYS');
            % through feval: the API check must not see an unknown literal command
            tc.verifyError(@() feval('iio_mex', 'no_such_command'), 'iio:ENOSYS'); %#ok<FVAL>
            ctx = iio.Context(tc.Fx.Uri);
            a = ctx.find_attr("hw_model");
            delete(ctx);
            tc.verifyError(@() a.read(), 'iio:EBADF');
        end

        function staysLoadedWhileInUse(tc)
            ctx = iio.Context(tc.Fx.Uri);
            clear mex %#ok<CLMEX>
            [~, loaded] = inmem;
            tc.verifyTrue(ismember('iio_mex', loaded));
            tc.verifyEqual(ctx.find_attr("hw_model").read(), 'Emulated Test Board');
        end

        function clearMexAndReload(tc)
            tc.verifyEqual(iio.internal.handle_count(), 0);    % loads the MEX file
            [~, loaded] = inmem;
            tc.verifyTrue(ismember('iio_mex', loaded));
            clear mex %#ok<CLMEX>
            [~, loaded] = inmem;
            tc.verifyFalse(ismember('iio_mex', loaded));
            ctx = iio.Context(tc.Fx.Uri);
            tc.verifyEqual(ctx.find_attr("hw_model").read(), 'Emulated Test Board');
            delete(ctx);
            tc.verifyEqual(iio.internal.handle_count(), 0);
        end

        function libiioStaysMapped(tc)
            ctx = iio.Context(tc.Fx.Uri);
            delete(ctx);
            clear mex %#ok<CLMEX>
            maps = fileread('/proc/self/maps');
            tc.verifyTrue(contains(maps, 'libiio.so.1'), 'libiio is no longer mapped after "clear mex"');
            paths = unique(regexp(maps, '/[^\n]*(?:libxml2|libzstd|libiio)[^\n]*', 'match'));
            for k = 1:numel(paths)
                tc.log(1, ['mapped: ' paths{k}]);
            end
        end

        function matlabXmlStillWorks(tc)
            ctx = iio.Context(tc.Fx.Uri);
            delete(ctx);
            clear mex %#ok<CLMEX>
            f = fullfile(tc.Fx.Dir, 'matlab_xml.xml');
            writestruct(struct('a', 1), f);
            s = readstruct(f);
            tc.verifyEqual(s.a, 1);
            doc = parseFile(matlab.io.xml.dom.Parser, f);
            tc.verifyTrue(isa(doc, 'matlab.io.xml.dom.Document'));
        end

        function destructorOrder(tc)
            % the context is referenced only by a.Parent
            a = iio.Context(tc.Fx.Uri).find_attr("num");
            tc.verifyEqual(iio.internal.handle_count(), 2);
            tc.verifyEqual(a.read(), '42');
            clear a
            tc.verifyEqual(iio.internal.handle_count(), 0);
        end

        function fiftyContexts(tc)
            v = '';
            for k = 1:50
                ctx = iio.Context(tc.Fx.Uri);
                v = ctx.find_attr("num").read();
                delete(ctx);
            end
            tc.verifyEqual(iio.internal.handle_count(), 0);
            tc.verifyEqual(v, '42');
        end
    end
end
