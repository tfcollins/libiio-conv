classdef Context < iio.internal.Object
    %CONTEXT An IIO context: one connection to a set of IIO devices.
    %   CTX = iio.Context(URI) creates the context that URI names, for example
    %   "ip:192.168.2.1", "usb:1.2.3", "local:" or "emu:/path/to/file.xml".
    %   CTX = iio.Context() lets libiio choose: the URI in the environment variable
    %   IIOD_REMOTE if it is set, else "local:".
    %
    %   CTX = iio.Context(URI, Name = Value) sets context parameters:
    %       Timeout           milliseconds for every I/O of this context; Inf = no timeout
    %       LogLevel          "nolog", "error", "warning", "info" or "debug"
    %       StderrLevel       messages at or above this level go to stderr
    %       TimestampLevel    messages at or above this level carry a timestamp
    %       XmlIncludeValues  true: the xml property carries the attribute values
    %
    %   The context is destroyed, with everything obtained from it, when CTX is
    %   deleted or goes out of scope. An object obtained from CTX keeps CTX alive.
    %
    %   The timeout belongs to the whole context: libiio has no timeout per buffer or
    %   per call. set_timeout therefore changes attribute I/O and every stream of
    %   this context.
    %
    %   See also iio.Device, iio.Attr.

    properties (Constant)
        TIMEOUT_BACKEND = 0             % set_timeout: use the backend's default
        TIMEOUT_INFINITE = -1           % set_timeout: never time out (Inf is accepted as well)
        TIMEOUT_NONBLOCK = -2147483648  % set_timeout: never wait
    end

    properties (SetAccess = immutable)
        uri char            % as given; '' when defaulted or adopted
        name char           % backend name: 'local', 'ip', 'usb', 'serial', 'emu', 'xml'
        description char
        version struct      % major, minor, patch (double), tag (char): of the remote side
    end

    properties (Dependent)
        xml         % char: the XML representation of the context
        attrs       % 1-by-N iio.Attr: the context attributes
        devices     % 1-by-N iio.Device
        params      % struct: log_level, stderr_level, timestamp_level, timeout_ms, flags
    end

    methods
        function obj = Context(uri, opts)
            arguments
                uri (1,1) string = ""
                opts.Timeout (1,1) double
                opts.LogLevel (1,1) string {mustBeMember(opts.LogLevel, ["nolog", "error", "warning", "info", "debug"])}
                opts.StderrLevel (1,1) string {mustBeMember(opts.StderrLevel, ["nolog", "error", "warning", "info", "debug"])}
                opts.TimestampLevel (1,1) string {mustBeMember(opts.TimestampLevel, ["nolog", "error", "warning", "info", "debug"])}
                opts.XmlIncludeValues (1,1) logical
                opts.AdoptHandle (1,1) uint64
            end
            % Handle is set before any other call: delete releases the context if one fails.
            if isfield(opts, 'AdoptHandle')
                obj.Handle = opts.AdoptHandle;
                obj.uri = '';
            elseif uri == ""
                obj.Handle = iio_mex('create_context', iio.internal.params(opts), []);
                obj.uri = '';
            else
                obj.Handle = iio_mex('create_context', iio.internal.params(opts), char(uri));
                obj.uri = char(uri);
            end
            h = obj.Handle;
            obj.name = iio_mex('context_get_name', h);
            obj.description = iio_mex('context_get_description', h);
            v = struct('major', 0, 'minor', 0, 'patch', 0, 'tag', '');
            v.major = iio_mex('context_get_version_major', h);
            v.minor = iio_mex('context_get_version_minor', h);
            v.patch = iio_mex('context_get_version_patch', h);
            v.tag = iio_mex('context_get_version_tag', h);
            obj.version = v;
        end

        function delete(obj)
            %DELETE Destroy the context and every object that came from it.
            if obj.Handle ~= 0
                iio_mex('context_destroy', obj.Handle);
            end
        end

        function s = get.xml(obj)
            s = iio_mex('context_get_xml', obj.Handle);
        end

        function a = get.attrs(obj)
            h = iio_mex('context_get_attrs', obj.Handle);
            a = iio.Attr.empty(1, 0);
            for k = 1:numel(h)
                a(k) = iio.Attr(obj, h(k));
            end
        end

        function d = get.devices(obj)
            h = iio_mex('context_get_devices', obj.Handle);
            d = iio.Device.empty(1, 0);
            for k = 1:numel(h)
                d(k) = iio.Device(obj, h(k));
            end
        end

        function p = get.params(obj)
            p = iio_mex('context_get_params', obj.Handle);
        end

        function dev = find_device(obj, name)
            %FIND_DEVICE Device with that id, label or name: an iio.Device, or a 1-by-0 iio.Device.
            arguments
                obj (1,1) iio.Context
                name (1,1) string
            end
            h = iio_mex('context_find_device', obj.Handle, char(name));
            if h == 0
                dev = iio.Device.empty(1, 0);
            else
                dev = iio.Device(obj, h);
            end
        end

        function a = find_attr(obj, name)
            %FIND_ATTR Context attribute of that name: an iio.Attr, or a 1-by-0 iio.Attr.
            arguments
                obj (1,1) iio.Context
                name (1,1) string
            end
            h = iio_mex('context_find_attr', obj.Handle, char(name));
            if h == 0
                a = iio.Attr.empty(1, 0);
            else
                a = iio.Attr(obj, h);
            end
        end

        function set_timeout(obj, timeout)
            %SET_TIMEOUT Timeout in milliseconds of every I/O of this context.
            %   TIMEOUT is a whole number of milliseconds, Inf or iio.Context.TIMEOUT_INFINITE
            %   (never time out), iio.Context.TIMEOUT_BACKEND (the backend's default) or
            %   iio.Context.TIMEOUT_NONBLOCK (never wait). It applies to attribute I/O and to
            %   every stream of this context.
            arguments
                obj (1,1) iio.Context
                timeout (1,1) double
            end
            iio_mex('context_set_timeout', obj.Handle, iio.internal.timeout_ms(timeout));
        end

        function ping(obj)
            %PING Check that the context still answers; throws when it does not.
            arguments
                obj (1,1) iio.Context
            end
            iio_mex('context_ping', obj.Handle);
        end
    end
end
