classdef ObjModelProbe < iio.internal.Object
    %OBJMODELPROBE Test-only class with the idioms every iio class uses; calls no gateway command.
    %   P = ObjModelProbe(TAG, Parent = OBJ, Handle = H, AdoptHandle = H2, Note = TEXT)
    %   Its delete method appends [TAG ' '] to the text that ObjModelProbe.log returns.

    properties (SetAccess = immutable)
        tag char
    end

    properties
        note char
    end

    properties (Dependent)
        parent      % the hidden base property Parent
        label       % ['L' tag]
    end

    methods
        function obj = ObjModelProbe(tag, opts)
            arguments
                tag (1,1) string = ""
                opts.Parent = []
                opts.Handle (1,1) uint64 = uint64(0)
                opts.AdoptHandle (1,1) uint64
                opts.Note (1,1) string = "note-" + tag
            end
            obj@iio.internal.Object(opts.Parent, opts.Handle);
            if isfield(opts, 'AdoptHandle')
                obj.Handle = opts.AdoptHandle;
            end
            obj.tag = char(tag);
            obj.note = char(opts.Note);
        end

        function p = get.parent(obj)
            p = obj.Parent;
        end

        function s = get.label(obj)
            s = ['L' obj.tag];
        end

        function delete(obj)
            ObjModelProbe.record(false, [obj.tag ' ']);
        end
    end

    methods (Static)
        function s = log(reset)
            %LOG Text the delete methods wrote so far; LOG(true) also clears it.
            if nargin < 1
                reset = false;
            end
            s = ObjModelProbe.record(reset, '');
        end
    end

    methods (Static, Access = private)
        function s = record(reset, add)
            persistent text
            if isempty(text)
                text = '';
            end
            text = [text add];
            s = text;
            if reset
                text = '';
            end
        end
    end
end
