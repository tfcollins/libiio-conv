function n = handle_count()
%HANDLE_COUNT Number of libiio objects the gateway holds at the moment.
%   N = iio.internal.handle_count() is 0 when every context, stream and block was
%   released; the gateway can be unloaded only then.
n = iio_mex('mex_handle_count');
end
