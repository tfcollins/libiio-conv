function shutdown()
%SHUTDOWN Destroy every libiio object the gateway holds, streams before contexts.
%   iio.internal.shutdown() leaves every iio object in the workspace invalid: a later
%   call on one raises iio:EBADF. Afterwards "clear mex" can unload the gateway.
iio_mex('mex_shutdown');
end
