classdef BufferStream < iio.internal.Object
    %BUFFERSTREAM Placeholder until its own task replaces this file.
    methods
        function obj = BufferStream(parent, handle)
            arguments
                parent = []
                handle (1,1) uint64 = uint64(0)
            end
            obj@iio.internal.Object(parent, handle);
        end
    end
end
