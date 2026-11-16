classdef Attr < iio.internal.Object
    %ATTR An attribute of a context, device, channel or buffer.
    %   Attr objects are returned by the attrs properties and the find_attr methods;
    %   they are not created directly. An Attr keeps the object it came from alive.
    %
    %   Reading:   read (text), read_raw (bytes), read_bool, read_longlong, read_double
    %   Writing:   write (by the class of the value), write_string, write_raw,
    %              write_bool, write_longlong, write_double
    %   The property value reads with read and writes with write.
    %
    %   64-bit integers never pass through double: read_longlong returns int64 and
    %   write_longlong sends an int64 or uint64 input digit for digit. write_double
    %   sends the shortest fixed-notation text that reads back as the same double.
    %
    %   See also iio.Context, iio.Device.

    properties (SetAccess = immutable)
        name char
    end

    properties (Dependent)
        filename        % char: the file name of the attribute in sysfs
        static_value    % char: the value libiio holds without I/O (context attributes); [] when there is none
        value           % get: read(); set: write(v)
    end

    methods
        function obj = Attr(parent, handle)
            arguments
                parent = []
                handle (1,1) uint64 = uint64(0)
            end
            obj@iio.internal.Object(parent, handle);
            if handle ~= 0
                obj.name = iio_mex('attr_get_name', handle);
            end
        end

        function s = get.filename(obj)
            s = iio_mex('attr_get_filename', obj.Handle);
        end

        function s = get.static_value(obj)
            s = iio_mex('attr_get_static_value', obj.Handle);
        end

        function v = get.value(obj)
            v = iio_mex('attr_read_raw', obj.Handle);
        end

        function set.value(obj, v)
            obj.write(v);
        end

        function s = read(obj)
            %READ Value of the attribute as text (char row vector; '' when it is empty).
            %   The value is read as UTF-8 text up to its first zero byte. A value that is
            %   not UTF-8 text is not preserved (invalid bytes become char(65533)): read_raw
            %   returns the bytes.
            arguments
                obj (1,1) iio.Attr
            end
            s = iio_mex('attr_read_raw', obj.Handle);
        end

        function b = read_raw(obj)
            %READ_RAW Value of the attribute as bytes (1-by-N uint8), exactly as libiio counted them.
            %   Device, channel and buffer attributes include the terminating zero byte;
            %   context attributes do not.
            arguments
                obj (1,1) iio.Attr
            end
            b = iio_mex('attr_read_raw', obj.Handle, true);
        end

        function tf = read_bool(obj)
            %READ_BOOL Value of the attribute as a logical scalar.
            arguments
                obj (1,1) iio.Attr
            end
            tf = iio_mex('attr_read_bool', obj.Handle);
        end

        function v = read_longlong(obj)
            %READ_LONGLONG Value of the attribute as an int64 scalar.
            arguments
                obj (1,1) iio.Attr
            end
            v = iio_mex('attr_read_longlong', obj.Handle);
        end

        function v = read_double(obj)
            %READ_DOUBLE Value of the attribute as a double scalar.
            arguments
                obj (1,1) iio.Attr
            end
            v = iio_mex('attr_read_double', obj.Handle);
        end

        function write(obj, v)
            %WRITE Write a value, in the form that fits its class.
            %   char or string      write_string
            %   logical             write_bool
            %   an integer class, or a whole-number real floating-point scalar below 2^63
            %                       write_longlong (2400000000 is sent as "2400000000")
            %   anything else       write_double
            arguments
                obj (1,1) iio.Attr
                v
            end
            if ischar(v) || isstring(v)
                obj.write_string(v);
            elseif islogical(v)
                obj.write_bool(v);
            elseif isinteger(v)
                obj.write_longlong(v);
            elseif isfloat(v) && isscalar(v) && isreal(v) && v == fix(v) && abs(v) < 2^63
                obj.write_longlong(v);
            else
                obj.write_double(v);
            end
        end

        function n = write_raw(obj, data)
            %WRITE_RAW Write bytes; N = WRITE_RAW(DATA) is the number of bytes written.
            %   Every element of DATA is sent, zero bytes included.
            arguments
                obj (1,1) iio.Attr
                data (1,:) uint8
            end
            n = iio_mex('attr_write_raw', obj.Handle, data);
        end

        function write_string(obj, s)
            %WRITE_STRING Write text.
            %   Text that contains a zero character (char(0)) raises iio:EINVAL: binary data
            %   is written with write_raw.
            arguments
                obj (1,1) iio.Attr
                s (1,1) string
            end
            iio_mex('attr_write_string', obj.Handle, char(s));
        end

        function write_bool(obj, tf)
            %WRITE_BOOL Write true or false.
            arguments
                obj (1,1) iio.Attr
                tf (1,1) logical
            end
            iio_mex('attr_write_bool', obj.Handle, tf);
        end

        function write_longlong(obj, v)
            %WRITE_LONGLONG Write a whole number in the range of int64.
            %   V is a real numeric scalar. An int64 or uint64 value is sent exactly; a value
            %   that is not a whole number, or is outside the range of int64, raises iio:EINVAL.
            arguments
                obj (1,1) iio.Attr
                v (1,1) {mustBeNumeric, mustBeReal}
            end
            iio_mex('attr_write_longlong', obj.Handle, v);
        end

        function write_double(obj, v, opts)
            %WRITE_DOUBLE Write a floating-point number.
            %   WRITE_DOUBLE(V) sends the shortest text in fixed notation that reads back as
            %   exactly V (0.1 as "0.1", 1e-9 as "0.000000001").
            %   WRITE_DOUBLE(V, Exact = false) sends V with six decimals, as libiio does.
            %   With Exact = true every digit is sent, and the Linux kernel truncates what a
            %   driver does not keep where six decimals would have been rounded: round a
            %   computed value first, or use Exact = false. A value that needs more than 40
            %   decimals is sent with an exponent, which kernel drivers refuse.
            %   NaN and Inf raise iio:EINVAL.
            arguments
                obj (1,1) iio.Attr
                v (1,1) double
                opts.Exact (1,1) logical = true
            end
            iio_mex('attr_write_double', obj.Handle, v, opts.Exact);
        end

        function r = get_range(obj)
            %GET_RANGE [min step max] of an "_available" attribute that holds a range.
            arguments
                obj (1,1) iio.Attr
            end
            r = iio_mex('attr_get_range', obj.Handle);
        end

        function c = get_available(obj)
            %GET_AVAILABLE 1-by-N cell of char: the values of an "_available" attribute that holds a list.
            arguments
                obj (1,1) iio.Attr
            end
            c = iio_mex('attr_get_available', obj.Handle);
        end
    end
end
