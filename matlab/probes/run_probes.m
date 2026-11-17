function run_probes()
%RUN_PROBES Run every probe matlab/probes/p_*.m and print one result line per probe.
%   A probe is a script or a function without arguments. For each one this prints
%       PROBE <name> OK
%   or
%       PROBE <name> ERROR <identifier>: <message>
%   Probes print their own findings as lines that start with "PROBE ". See README.md.
here = fileparts(mfilename('fullpath'));
files = dir(fullfile(here, 'p_*.m'));
for k = 1:numel(files)
    [~, name] = fileparts(files(k).name);
    try
        runOne(fullfile(here, files(k).name));
        fprintf('PROBE %s OK\n', name);
    catch err
        fprintf('PROBE %s ERROR %s: %s\n', name, err.identifier, strrep(err.message, newline, ' | '));
    end
end
end

function runOne(probeFile)
% In its own workspace, so that a probe script cannot change the loop above.
run(probeFile);
end
