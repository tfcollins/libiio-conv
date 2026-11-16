classdef (Abstract) IioTestCase < matlab.unittest.TestCase
    %IIOTESTCASE Base of every iio test class.
    %   Each test method gets a fresh EmuFixture in tc.Fx, and fails when it leaves a
    %   gateway handle behind.

    properties
        Fx      % EmuFixture of the running test method
    end

    methods (TestMethodSetup)
        function applyEmuFixture(tc)
            tc.Fx = tc.applyFixture(EmuFixture);
        end
    end

    methods (TestMethodTeardown)
        function checkNoHandleIsLeft(tc)
            n = iio.internal.handle_count();
            iio.internal.shutdown();    % the next test starts clean whatever this one left
            tc.verifyEqual(n, 0, 'iio objects leaked');
        end
    end

    methods
        function verifyIioError(tc, fcn, errnoName)
            %VERIFYIIOERROR FCN must throw the identifier "iio:<errnoName>".
            tc.verifyError(fcn, char("iio:" + errnoName));
        end
    end
end
