classdef tObjectModel < IioTestCase
    %TOBJECTMODEL The M idioms every iio class relies on, without any gateway handle:
    %   equality, object arrays, empties, validity, parent lifetime, argument forms.
    %   Objects with a made-up handle are ObjModelProbe objects, which call no gateway
    %   command. A real iio class reads its name from the gateway in its constructor, so
    %   it is built here only in the forms X() and X.empty, which hold no handle.

    methods (Test)
        function equality(tc)
            a = ObjModelProbe("a", Handle = uint64(5));
            b = ObjModelProbe("b", Handle = uint64(5));
            c = ObjModelProbe("c", Handle = uint64(6));
            tc.verifyEqual(a == b, true);
            tc.verifyEqual(a ~= c, true);
            tc.verifyEqual([a c] == a, [true false]);
            tc.verifyEqual([a c] ~= a, [false true]);
            tc.verifyEqual([a c] == [b c], [true true]);
            tc.verifyClass(a == b, 'logical');
            tc.verifyClass([a c] ~= a, 'logical');
        end

        function objectArrays(tc)
            c = ObjModelProbe("c", Handle = uint64(6));
            arr = ObjModelProbe.empty(1, 0);
            arr(3) = c;     % elements 1 and 2 come from the constructor form ObjModelProbe()
            tc.verifySize(arr, [1 3]);
            tc.verifyClass(arr, 'ObjModelProbe');
            tc.verifyEqual(iio.internal.Object.handles(arr), uint64([0 0 6]));
            % the same growth with a real class: every element is the form iio.Channel()
            chans = iio.Channel.empty(1, 0);
            chans(2) = iio.Channel();
            tc.verifySize(chans, [1 2]);
            tc.verifyClass(chans, 'iio.Channel');
            tc.verifyEqual(iio.internal.Object.handles(chans), uint64([0 0]));
        end

        function empties(tc)
            e = iio.Attr.empty(1, 0);
            tc.verifySize(e, [1 0]);
            tc.verifyClass(e, 'iio.Attr');
            tc.verifyTrue(isempty(e));
            tc.verifySize(e == e, [1 0]);
            tc.verifySize([e iio.Attr()], [1 1]);
        end

        function validity(tc)
            a = ObjModelProbe("a", Handle = uint64(5));
            b = ObjModelProbe("b", Handle = uint64(5));
            c = ObjModelProbe("c", Handle = uint64(6));
            tc.verifyTrue(isvalid(a));
            delete(a);
            tc.verifyFalse(isvalid(a));
            tc.verifyEqual(isvalid([b c]), [true true]);
        end

        function parentKeepsAliveAndDiesLast(tc)
            ObjModelProbe.log(true);
            p = ObjModelProbe("p");
            k = ObjModelProbe("k", Parent = p);
            clear p
            tc.verifyEqual(ObjModelProbe.log(), '');    % the child keeps its parent
            tc.verifyEqual(k.parent.tag, 'p');
            tc.verifyTrue(isprop(k, 'parent'));
            tc.verifyTrue(isprop(k, 'Parent'));
            clear k
            tc.verifyEqual(ObjModelProbe.log(true), 'k p ');    % child first, then parent
        end

        function argumentForms(tc)
            tc.verifyEqual(ObjModelProbe("x").note, 'note-x');
            tc.verifyEqual(ObjModelProbe("x", Note = "n").note, 'n');
            o = ObjModelProbe(AdoptHandle = uint64(9));   % the form iio.Context(AdoptHandle = h) relies on
            tc.verifyEqual(o.Handle, uint64(9));
            tc.verifyEqual(o.tag, '');
            tc.verifyEqual(ObjModelProbe("x", Handle = uint64(4)).Handle, uint64(4));
        end

        function listExpansion(tc)
            objs = [ObjModelProbe("a") ObjModelProbe("b")];
            tc.verifyEqual({objs.tag}, {'a', 'b'});       % immutable property
            tc.verifyEqual({objs.label}, {'La', 'Lb'});   % Dependent property
        end
    end
end
