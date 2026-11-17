classdef (Abstract) Object < handle
    %OBJECT Base of every iio class: a gateway handle plus the MATLAB object that keeps it valid.

    properties (SetAccess = protected, Hidden)
        Handle (1,1) uint64 = uint64(0)   % gateway handle id, 0 = none
        Parent = []                       % iio object this one came from; keeps it alive
    end

    methods
        function obj = Object(parent, handle)
            if nargin >= 2
                obj.Parent = parent;
                obj.Handle = handle;
            end
        end

        function tf = eq(a, b)
            %EQ True where both wrap the same libiio object.
            tf = iio.internal.Object.handles(a) == iio.internal.Object.handles(b);
        end

        function tf = ne(a, b)
            tf = ~eq(a, b);
        end
    end

    methods (Static, Hidden)
        function h = handles(objs)
            %HANDLES uint64 array, same size as OBJS, of their gateway ids.
            h = zeros(size(objs), 'uint64');
            for k = 1:numel(objs)
                h(k) = objs(k).Handle;
            end
        end
    end
end
