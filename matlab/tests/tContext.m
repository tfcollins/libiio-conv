classdef tContext < IioTestCase
    %TCONTEXT iio.Context: identity, attribute and device lists, parameters, timeout, XML, lifetime.

    methods (Test)
        function identity(tc)
            ctx = iio.Context(tc.Fx.Uri);
            tc.verifyEqual(ctx.uri, tc.Fx.Uri);
            tc.verifyEqual(ctx.name, 'emu');
            tc.verifyEqual(ctx.description, 'Emulated fixture & test context');
            tc.verifyEqual(ctx.version, struct('major', 1, 'minor', 2, 'patch', 3, 'tag', 'v1.2.3-fixture'));
        end

        function charAndStringUri(tc)
            a = iio.Context(char(tc.Fx.Uri));
            tc.verifyEqual(a.name, 'emu');
            tc.verifyClass(a.uri, 'char');
            b = iio.Context(string(tc.Fx.Uri));
            tc.verifyEqual(b.name, 'emu');
            tc.verifyEqual(b.uri, char(tc.Fx.Uri));
            tc.verifyClass(b.uri, 'char');
        end

        function attrs(tc)
            ctx = iio.Context(tc.Fx.Uri);
            a = ctx.attrs;
            tc.verifyClass(a, 'iio.Attr');
            tc.verifySize(a, [1 4]);
            tc.verifyEqual({a.name}, {'escaped', 'fixture', 'hw_model', 'num'});
            tc.verifyTrue(ctx.find_attr("num") == a(4));
            tc.verifyTrue(ctx.find_attr('num') == ctx.attrs(4));
            none = ctx.find_attr("nope");
            tc.verifyTrue(isempty(none));
            tc.verifyClass(none, 'iio.Attr');
        end

        function devices(tc)
            ctx = iio.Context(tc.Fx.Uri);
            d = ctx.devices;
            tc.verifyClass(d, 'iio.Device');
            tc.verifySize(d, [1 8]);
            tc.verifyEqual(numel(ctx.devices), 8);
            phy = ctx.find_device("emu-phy");
            tc.verifyClass(phy, 'iio.Device');
            tc.verifySize(phy, [1 1]);
            tc.verifyTrue(phy == ctx.find_device("iio:device2"));
            tc.verifyTrue(phy == ctx.find_device("phy"));
            tc.verifyTrue(phy == ctx.find_device('phy'));
            tc.verifyTrue(phy == d(4));
            tc.verifyTrue(phy ~= d(1));
            none = ctx.find_device("nope");
            tc.verifyTrue(isempty(none));
            tc.verifyClass(none, 'iio.Device');
            tc.verifySize(none, [1 0]);
        end

        function params(tc)
            ctx = iio.Context(tc.Fx.Uri);
            p = ctx.params;
            tc.verifyClass(p, 'struct');
            tc.verifyEqual(p.timeout_ms, 1000);
            tc.verifyEqual(p.log_level, 4);
            tc.verifyEqual(p.flags, 0);
            b = iio.Context(tc.Fx.Uri, Timeout = 123, LogLevel = "nolog", XmlIncludeValues = true);
            q = b.params;
            tc.verifyEqual(q.timeout_ms, 123);
            tc.verifyEqual(q.log_level, 1);
            tc.verifyEqual(q.flags, 1);
            c = iio.Context(tc.Fx.Uri, Timeout = Inf);
            tc.verifyEqual(c.params.timeout_ms, -1);
        end

        function timeoutIsPerContext(tc)
            % libiio v1: the timeout belongs to one context, not to a buffer or a call
            a = iio.Context(tc.Fx.Uri);
            b = iio.Context(tc.Fx.Uri);
            a.set_timeout(2500);
            tc.verifyEqual(a.params.timeout_ms, 2500);
            tc.verifyEqual(b.params.timeout_ms, 1000);
            a.set_timeout(Inf);
            tc.verifyEqual(a.params.timeout_ms, -1);
            a.set_timeout(iio.Context.TIMEOUT_NONBLOCK);
            tc.verifyEqual(a.params.timeout_ms, 0);
            a.set_timeout(iio.Context.TIMEOUT_BACKEND);
            tc.verifyEqual(a.params.timeout_ms, 1000);
            tc.verifyError(@() a.set_timeout(-5), 'iio:EINVAL');
            tc.verifyError(@() a.set_timeout(1.5), 'iio:EINVAL');
            tc.verifyEqual(a.params.timeout_ms, 1000);
            tc.verifyEqual(b.params.timeout_ms, 1000);
        end

        function xml(tc)
            ctx = iio.Context(tc.Fx.Uri);
            x = ctx.xml;
            tc.verifyClass(x, 'char');
            tc.verifyEqual(size(x, 1), 1);
            tc.verifyTrue(startsWith(x, '<?xml version="1.0"'));
            tc.verifyTrue(contains(x, 'emu-phy'));
            tc.verifyFalse(contains(x, 'value="auto"'));
            b = iio.Context(tc.Fx.Uri, XmlIncludeValues = true);
            tc.verifyTrue(contains(b.xml, 'value="auto"'));
        end

        function ping(tc)
            ctx = iio.Context(tc.Fx.Uri);
            ctx.ping();
            x = iio.Context(tc.Fx.XmlUri);
            tc.verifyError(@() x.ping(), 'iio:ENOTSUP');
            tc.verifyEqual(x.name, 'xml');
        end

        function createErrors(tc)
            tc.verifyError(@() iio.Context("bogus:foo"), 'iio:ENOSYS');
            tc.verifyError(@() iio.Context("emu:/nonexistent.xml"), 'iio:EINVAL');
            % argument validation: an identifier of MATLAB itself, whatever its exact name
            id = '';
            try
                iio.Context(tc.Fx.Uri, LogLevel = "loud");
            catch err
                id = err.identifier;
            end
            tc.verifyTrue(startsWith(id, 'MATLAB:'), ['identifier: ' id]);
            tc.verifyEqual(iio.internal.handle_count(), 0);
        end

        function deleteInvalidatesChildren(tc)
            ctx = iio.Context(tc.Fx.Uri);
            d = ctx.find_device("emu-phy");
            delete(ctx);
            tc.verifyFalse(isvalid(ctx));
            tc.verifyError(@() d.is_trigger, 'iio:EBADF');
            delete(ctx);    % again: nothing happens
            tc.verifyEqual(iio.internal.handle_count(), 0);
        end

        function childKeepsContextAlive(tc)
            % the context is referenced only by d.Parent
            d = iio.Context(tc.Fx.Uri).find_device("emu-phy");
            tc.verifyEqual(d.id, 'iio:device2');
            tc.verifyEqual(iio.internal.handle_count(), 2);
            clear d
            tc.verifyEqual(iio.internal.handle_count(), 0);
        end
    end
end
