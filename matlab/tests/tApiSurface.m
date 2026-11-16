classdef tApiSurface < IioTestCase
    %TAPISURFACE Every MATLAB member that matlab/api-map.tsv names exists.
    %   The map has one row per function of libiio's iio.h. Its third column lists the
    %   members of package iio that reach the function: iio.<Class>.<member> (a method
    %   or a property), iio.<Class> (the constructor) or iio.<function>; "-" when the
    %   function has none.

    methods (Test)
        function rowCount(tc)
            rows = mapRows();
            tc.verifyEqual(rows(1, :), ["c_function" "command" "matlab" "status" "notes"]);
            tc.verifySize(rows, [142 5]);       % the header line and 141 functions
            tc.verifyEqual(numel(unique(rows(2:end, 1))), 141);
            status = rows(2:end, 4);
            tc.verifyEqual(nnz(status == "exposed"), 124);
            tc.verifyEqual(nnz(status == "not_exposed"), 12);
            tc.verifyEqual(nnz(status == "internal"), 5);
            % a function is exposed exactly when it has a MATLAB member
            tc.verifyEqual(rows(2:end, 3) ~= "-", status == "exposed");
        end

        function everyMemberExists(tc)
            rows = mapRows();
            checked = 0;
            for r = 2:size(rows, 1)
                if rows(r, 3) == "-"
                    continue
                end
                members = strtrim(split(rows(r, 3), ","));
                for k = 1:numel(members)
                    name = char(members(k));
                    tc.verifyTrue(memberExists(name), [char(rows(r, 1)) ': no ' name]);
                    checked = checked + 1;
                end
            end
            tc.verifyGreaterThanOrEqual(checked, 124);
        end

        function namesThatDoNotExist(tc)
            % the check itself tells a missing name from an existing one
            tc.verifyTrue(memberExists('iio.Context.find_device'));
            tc.verifyTrue(memberExists('iio.Context.xml'));
            tc.verifyTrue(memberExists('iio.Context'));
            tc.verifyTrue(memberExists('iio.version'));
            tc.verifyTrue(memberExists('iio.internal.handle_count'));
            tc.verifyFalse(memberExists('iio.Context.no_such_member'));
            tc.verifyFalse(memberExists('iio.NoSuchClass'));
            tc.verifyFalse(memberExists('iio.NoSuchClass.read'));
            tc.verifyFalse(memberExists('iio.no_such_function'));
            tc.verifyFalse(memberExists('iio.internal.no_such_function'));
        end
    end
end

function rows = mapRows()
%MAPROWS The lines of matlab/api-map.tsv as an N-by-5 string array, the header line first.
file = fullfile(fileparts(mfilename('fullpath')), '..', 'api-map.tsv');
lines = splitlines(string(fileread(file)));
lines = lines(strlength(lines) > 0);
rows = strings(numel(lines), 5);
for k = 1:numel(lines)
    cols = split(lines(k), sprintf('\t'));
    assert(numel(cols) == 5, 'tApiSurface:columns', 'Line %d of %s has %d columns.', k, file, numel(cols));
    rows(k, :) = reshape(cols, 1, 5);
end
end

function tf = memberExists(name)
%MEMBEREXISTS True when NAME (char) is a class, a method or property of a class, or a
%   function of package iio or iio.internal.
parts = strsplit(name, '.');
tf = false;
if numel(parts) < 2 || ~strcmp(parts{1}, 'iio')
    return
end
first = parts{2}(1);
if strcmp(parts{2}, 'internal') || first ~= upper(first)
    % a package function
    tf = ~isempty(which(name));
    return
end
mc = meta.class.fromName(['iio.' parts{2}]);
if isempty(mc)
    return
end
if numel(parts) == 2
    tf = true;
elseif numel(parts) == 3
    tf = any(strcmp(parts{3}, {mc.MethodList.Name})) || any(strcmp(parts{3}, {mc.PropertyList.Name}));
end
end
