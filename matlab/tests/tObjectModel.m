classdef tObjectModel < IioTestCase
    %TOBJECTMODEL The M idioms every iio class relies on, without any gateway handle:
    %   equality, object arrays, empties, validity, parent lifetime, argument forms.

    methods (Test)
        function equality(tc)
            a = iio.Channel([], uint64(5));
            b = iio.Channel([], uint64(5));
            c = iio.Channel([], uint64(6));
            tc.verifyEqual(a == b, true);
            tc.verifyEqual(a ~= c, true);
            tc.verifyEqual([a c] == a, [true false]);
            tc.verifyEqual([a c] ~= a, [false true]);
            tc.verifyEqual([a c] == [b c], [true true]);
            tc.verifyClass(a == b, 'logical');
            tc.verifyClass([a c] ~= a, 'logical');
        end

        function objectArrays(tc)
            c = iio.Channel([], uint64(6));
            arr = iio.Channel.empty(1, 0);
            arr(3) = c;     % elements 1 and 2 come from the constructor form iio.Channel()
            tc.verifySize(arr, [1 3]);
            tc.verifyClass(arr, 'iio.Channel');
            tc.verifyEqual(iio.internal.Object.handles(arr), uint64([0 0 6]));
        end

        function empties(tc)
            e = iio.Attr.empty(1, 0);
            tc.verifySize(e, [1 0]);
            tc.verifyClass(e, 'iio.Attr');
            tc.verifyTrue(isempty(e));
            tc.verifySize(e == e, [1 0]);
            tc.verifySize([e iio.Attr([], uint64(1))], [1 1]);
        end

        function validity(tc)
            a = iio.Channel([], uint64(5));
            b = iio.Channel([], uint64(5));
            c = iio.Channel([], uint64(6));
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
