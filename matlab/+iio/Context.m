classdef Context < iio.internal.Object
    %CONTEXT An IIO context: one connection to a set of IIO devices.
    %   CTX = iio.Context(URI) creates the context that URI names, for example
    %   "ip:192.168.2.1", "usb:1.2.3", "local:" or "emu:/path/to/file.xml".
    %   The context is destroyed, with everything obtained from it, when CTX is
    %   deleted or goes out of scope. An object obtained from CTX keeps CTX alive.
    %
    %   First version (context creation and context attributes only).

    methods
        function obj = Context(uri)
            arguments
                uri (1,1) string
            end
            obj.Handle = iio_mex('create_context', [], char(uri));
        end

        function delete(obj)
            %DELETE Destroy the context and every object that came from it.
            if obj.Handle ~= 0
                iio_mex('context_destroy', obj.Handle);
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
    end
end
