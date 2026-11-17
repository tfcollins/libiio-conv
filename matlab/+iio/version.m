function v = version()
%VERSION Version of the libiio library the gateway is linked to.
%   V = iio.version() returns a scalar struct with the fields major, minor and patch
%   (double) and tag (char, the git tag or commit of the build, e.g. 'v1.0').
%
%   This is the library in this MATLAB process. The version of the remote side of a
%   context (an IIO daemon, for example) is the property version of iio.Context.
%
%   See also iio.Context, iio.backends.
v = struct('major', 0, 'minor', 0, 'patch', 0, 'tag', '');
v.major = iio_mex('context_get_version_major', uint64(0));
v.minor = iio_mex('context_get_version_minor', uint64(0));
v.patch = iio_mex('context_get_version_patch', uint64(0));
v.tag = char(iio_mex('context_get_version_tag', uint64(0)));
end
