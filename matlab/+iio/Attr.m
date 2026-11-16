classdef Attr < iio.internal.Object
    %ATTR An attribute of a context, device, channel or buffer.
    %   Attr objects are returned by the find_attr methods; they are not created
    %   directly. An Attr keeps the object it came from alive.
    %
    %   First version (reading as text only).

    methods
        function obj = Attr(parent, handle)
            arguments
                parent = []
                handle (1,1) uint64 = uint64(0)
            end
            obj@iio.internal.Object(parent, handle);
        end

        function s = read(obj)
            %READ Value of the attribute as text (char row vector).
            arguments
                obj (1,1) iio.Attr
            end
            s = iio_mex('attr_read_raw', obj.Handle);
        end
    end
end
