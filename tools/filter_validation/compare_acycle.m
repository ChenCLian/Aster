function report = compare_acycle(acycleRoot, outputFile)
% Optional MATLAB + Signal Processing Toolbox check against the USER-SUPPLIED
% corrected-bandpass-v2 reference. It is not an assertion about official Acycle.
% Usage: addpath('tools/filter_validation'); compare_acycle('path/to/Acycle_3.1.1')
% The same independent fixtures are checked against Aster C++ by filter_audit.
if nargin < 2, outputFile = ''; end
root = fileparts(fileparts(fileparts(mfilename('fullpath'))));
oldPath = path; cleanup = onCleanup(@() path(oldPath)); %#ok<NASGU>
addpath(fullfile(acycleRoot,'code','seriesanalysis'),'-begin');
assert(exist('acycleBandpassFilter','file') == 2, 'Missing corrected-bandpass-v2 reference');
fixture = jsondecode(fileread(fullfile(root,'analysis_tests','fixtures','filter_reference.json')));
results = struct('method',{},'n',{},'q',{},'filteredError',{},'envelopeError',{},'quadratureError',{});
for i = 1:numel(fixture.cases)
    c = fixture.cases(i); y = c.input(:); x = (0:numel(y)-1)' * c.spacing;
    options = struct('method',c.method,'lower_frequency',c.low,'upper_frequency',c.high);
    if strcmp(c.method,'taner')
        options.method = 'taner_hilbert'; options.rolloff_exponent = c.q;
    end
    [actual,meta] = acycleBandpassFilter([x,y],options);
    assert(strcmp(meta.implementation,'corrected-bandpass-v2'), 'Unexpected reference implementation');
    q = imag(hilbert(actual.filtered));
    errors = [max(abs(actual.filtered(:)-c.filtered(:))), ...
              max(abs(actual.envelope(:)-c.envelope(:))), max(abs(q(:)-c.quadrature(:)))];
    assert(all(errors < 5e-11), 'Reference mismatch in case %d', i);
    results(i) = struct('method',c.method,'n',c.n,'q',c.q, ...
        'filteredError',errors(1),'envelopeError',errors(2),'quadratureError',errors(3)); %#ok<AGROW>
end
report = struct('reference','user supplied corrected-bandpass-v2','matlab',version, ...
                'cases',results,'passed',true);
if ~isempty(outputFile)
    assert(~isfile(outputFile), 'Refusing to overwrite existing output');
    f = fopen(outputFile,'w'); assert(f>=0,'Cannot open report');
    closeFile = onCleanup(@() fclose(f)); %#ok<NASGU>
    fprintf(f,'%s\n',jsonencode(report));
end
disp(report);
end
