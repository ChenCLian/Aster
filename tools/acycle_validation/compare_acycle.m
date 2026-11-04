function report = compare_acycle(acycleRoot, resultsFile, reportFile)
% Compare the uploaded Acycle source with actual Aster C++ results.
% Requires MATLAB + Signal Processing Toolbox. Missing chi2inv is handled
% by a temporary base-MATLAB gammaincinv adapter, recorded in the report.
% No Acycle source or permanent MATLAB path is changed.
if nargin<2
    error('Pass the current C++ results JSON generated with ASTER_ACYCLE_EXPORT as resultsFile.');
end
if nargin<3, reportFile=fullfile(pwd,'aster_acycle_comparison.json'); end
originalPath=path;
pathCleanup=onCleanup(@() path(originalPath)); %#ok<NASGU>
addpath(genpath(fullfile(acycleRoot,'code')));
needed={'pmtm','redconfML','specswa','wavelet','wave_signif','ar1nv','chisquare_inv'};
sources=struct();
for i=1:numel(needed)
    location=which(needed{i});
    assert(~isempty(location),'Missing function %s',needed{i});
    sources.(needed{i})=location;
    if i>1
        assert(startsWith(lower(strrep(location,'\','/')), ...
            [lower(stripTrailingSlash(strrep(char(acycleRoot),'\','/'))) '/']), ...
            'Function %s is shadowed: %s',needed{i},location);
    end
end
[compatDir,dependency]=installChiSquareFallback();
compatCleanup=onCleanup(@() removeCompat(compatDir)); %#ok<NASGU>
sources.chi2inv=which('chi2inv');
document=jsondecode(fileread(resultsFile));
cases=document.cases;
if ~iscell(cases), cases=num2cell(cases); end
rows=cell(size(cases));
for i=1:numel(cases)
    c=cases{i};
    if strcmp(c.method,'mtm'), row=compareMtm(c); else, row=compareCwt(c); end
    row.status=classify(row);
    rows{i}=row;
    fprintf('%s: %s\n',c.name,row.status);
end
statuses=cellfun(@(r) r.status,rows,'UniformOutput',false);
summary=struct('cases',numel(rows),'matched',sum(strcmp(statuses,'matched')), ...
    'matched_with_quantile_approximation',sum(strcmp(statuses,'matched_with_quantile_approximation')), ...
    'incomplete',sum(strcmp(statuses,'incomplete')),'discrepancy',sum(strcmp(statuses,'discrepancy')));
report=struct('bridge_version',2,'matlab_version',version,'sources',sources, ...
    'dependencies',dependency,'cpp_results_file',resultsFile,'summary',summary,'cases',{rows}, ...
    'note',['Original comparison tolerances are retained. CWT quantile-rounding ', ...
    'differences are reported separately from the exact-quantile formula check. ', ...
    'AR1 and SWA run independently so one failure does not hide other results. ', ...
    'This is numerical comparison, not false-positive-rate calibration. ', ...
    'The original Acycle FDR lookup-grid path is not directly compared here.']);
fid=fopen(reportFile,'w','n','UTF-8'); assert(fid>=0,'Cannot open report file');
fileCleanup=onCleanup(@() fclose(fid)); %#ok<NASGU>
fprintf(fid,'%s\n',jsonencode(report,'PrettyPrint',true));
fprintf('Report: %s\n',reportFile);
end

function row=emptyRow(c)
row=struct('name',c.name,'method',c.method,'comparisons',struct(), ...
    'diagnostics',struct(),'actual_matlab',struct(),'errors',struct());
end

function row=compareMtm(c)
row=emptyRow(c); x=c.x(:); y=c.y(:); p=c.parameters;
dt=mean(diff(x)); y=y-mean(y); conversion=2*pi*dt;
power=[]; f=[];
try
    if p.nw==1
        [power,w]=pmtm(y,p.nw,p.nfft,'DropLastTaper',false);
    else
        [power,w]=pmtm(y,p.nw,p.nfft);
    end
    f=w/conversion;
    row.actual_matlab.frequency=f;
    row.actual_matlab.spectrum=power*conversion;
    row.comparisons.frequency=compare(c.frequency,f,2e-12);
    row.comparisons.spectrum=compare(c.actual_cpp.spectrum,power*conversion,2e-5);
catch e, row.errors.spectrum=errorText(e); end
if ~isempty(power)
    try
        row.diagnostics.adaptive=adaptiveDiagnostic(y,p.nw,p.nfft,c.tapers,power);
    catch e, row.errors.adaptive_diagnostic=errorText(e); end
    % SWA must still run if the independent AR1 fit or quantile call fails.
    if numel(power)>=33
        try
            background=specswa(f,log10(power),numel(y),false);
            row.actual_matlab.swa_background=background*conversion;
            row.comparisons.swa_background=compare(c.actual_cpp.swa_background,background*conversion,2e-5);
            threshold=background*conversion*chi2inv(.95,2*c.tapers)/(2*c.tapers);
            row.actual_matlab.swa_95=threshold;
            row.comparisons.swa_95=compare(c.actual_cpp.swa_95,threshold,2e-5);
        catch e, row.errors.swa=errorText(e); end
    end
end
try
    [rho,amplitude,smoothed,robust]=redconfML(y,dt,p.nw,p.nfft,1+double(p.logfit),.2,1/(2*dt),0);
    row.diagnostics.ar1=struct('rho',rho,'scale',amplitude,'optimization_function',which('lsqcurvefit'));
    row.actual_matlab.ar1_smoothed=smoothed(:,3)*conversion;
    row.actual_matlab.ar1_background=robust(:,3)*conversion;
    row.actual_matlab.ar1_95=robust(:,5)*conversion;
    row.comparisons.ar1_smoothed=compare(c.actual_cpp.ar1_smoothed,smoothed(:,3)*conversion,2e-5);
    row.comparisons.ar1_background=compare(c.actual_cpp.ar1_background,robust(:,3)*conversion,2e-3);
    row.comparisons.ar1_95=compare(c.actual_cpp.ar1_95,robust(:,5)*conversion,2e-3);
catch e, row.errors.ar1=errorText(e); end
end

function result=adaptiveDiagnostic(y,nw,nfft,k,matlabPower)
% Independent formula reconstruction using the public DPSS/FFT APIs. This
% records the convergence mechanism; it does not modify pmtm or its result.
[tapers,eigen]=dpss(numel(y),nw,k);
individual=abs(fft(y.*tapers,nfft,1)).^2;
variance=mean(y.^2); bins=floor(nfft/2)+1;
result=struct();
for domain=1:2
    power=mean(individual(:,1:min(2,k)),2);
    converged=false;
    for iteration=1:1000
        weights=eigen(:)'.*(power./(power*eigen(:)'+variance*(1-eigen(:)'))).^2;
        next=sum(weights.*individual,2)./sum(weights,2);
        delta=abs(next-power); power=next;
        if domain==1, change=sum(delta(1:bins)); else, change=sum(delta); end
        if change<.0005*variance, converged=true; break; end
    end
    one=power(1:bins)/(2*pi); one(2:end)=2*one(2:end);
    if mod(nfft,2)==0, one(end)=one(end)/2; end
    diagnostic=struct('iterations',iteration,'converged',converged, ...
        'versus_pmtm',compare(one,matlabPower,2e-10));
    if domain==1, result.legacy_half_spectrum=diagnostic; else, result.full_spectrum=diagnostic; end
end
end

function row=compareCwt(c)
row=emptyRow(c); x=c.x(:); y=c.y(:); p=c.parameters; dt=mean(diff(x)); n=numel(y);
try
    [w,period,scale,coi]=wavelet(y,dt,1,p.dj,2*dt,round(log2(.17*n)/p.dj),upper(p.mother),p.parameter);
    raw=abs(w).^2; corrected=raw./scale(:); normalized=corrected/var(y);
    row.actual_matlab.raw_power=reshape(raw.',[],1);
    row.actual_matlab.scale_corrected_power=reshape(corrected.',[],1);
    row.actual_matlab.power=reshape(normalized.',[],1);
    row.actual_matlab.period=period; row.actual_matlab.coi=coi;
    row.comparisons.raw_power=compare(c.actual_cpp.raw_power,row.actual_matlab.raw_power,2e-8);
    row.comparisons.scale_corrected_power=compare(c.actual_cpp.scale_corrected_power,row.actual_matlab.scale_corrected_power,2e-8);
    row.comparisons.power=compare(c.actual_cpp.power,row.actual_matlab.power,2e-8);
    row.comparisons.period=compare(c.period,period,2e-12);
    row.comparisons.coi_interior=compare(c.actual_cpp.coi(2:end-1),coi(2:end-1),2e-12);
catch e, row.errors.wavelet=errorText(e); return; end
try
    lag=ar1nv(y);
    [significance,background]=wave_signif(1,dt,scale,0,lag,-1,-1,upper(p.mother),p.parameter);
    ratio=raw./(var(y)*significance(:));
    dof=2; if strcmpi(p.mother,'dog'), dof=1; end
    legacyQuantile=chisquare_inv(.95,dof);
    exactQuantile=2*gammaincinv(.95,dof/2);
    exactRatio=raw./(var(y)*background(:)*(exactQuantile/dof));
    row.actual_matlab.significance_ratio=reshape(ratio.',[],1);
    row.actual_matlab.significance_ratio_exact_quantile=reshape(exactRatio.',[],1);
    row.comparisons.significance_ratio=compare(c.actual_cpp.significance_ratio,row.actual_matlab.significance_ratio,2e-8);
    row.comparisons.significance_ratio_exact_quantile=compare(c.actual_cpp.significance_ratio,row.actual_matlab.significance_ratio_exact_quantile,2e-8);
    row.diagnostics.quantile=struct('degrees_of_freedom',dof,'acycle_quantile',legacyQuantile, ...
        'exact_quantile',exactQuantile,'predicted_signed_ratio_difference',legacyQuantile/exactQuantile-1, ...
        'note','Same threshold formula; Acycle uses rounded/finite-tolerance chisquare_inv, Aster uses GSL.');
catch e, row.errors.significance=errorText(e); end
end

function [directory,info]=installChiSquareFallback()
directory=''; info=struct('chi2inv_fallback_used',false,'original_error','');
try
    value=chi2inv(.95,2);
    assert(isfinite(value) && abs(value+2*log(.05))<1e-10,'Unexpected chi2inv result');
catch e
    info.original_error=errorText(e);
    directory=tempname; mkdir(directory);
    try
        copyfile(fullfile(fileparts(mfilename('fullpath')),'chi2inv.m.in'),fullfile(directory,'chi2inv.m'));
        addpath(directory,'-begin'); rehash;
        assert(abs(chi2inv(.95,2)+2*log(.05))<1e-12,'Compatibility quantile check failed');
        info.chi2inv_fallback_used=true;
        info.fallback_definition='2*gammaincinv(p,nu/2); base MATLAB; temporary path only';
    catch failure
        removeCompat(directory); rethrow(failure);
    end
end
end

function removeCompat(directory)
if isempty(directory) || ~isfolder(directory), return; end
if any(strcmp(strsplit(path,pathsep),directory)), rmpath(directory); end
rmdir(directory,'s');
end
function result=stripTrailingSlash(value)
result=regexprep(value,'/+$','');
end
function value=errorText(exception)
value=getReport(exception,'basic','hyperlinks','off');
end
function status=classify(row)
if ~isempty(fieldnames(row.errors)), status='incomplete'; return; end
names=fieldnames(row.comparisons); failed={};
for i=1:numel(names)
    if ~row.comparisons.(names{i}).pass, failed{end+1}=names{i}; end %#ok<AGROW>
end
if isempty(names), status='incomplete';
elseif isempty(failed), status='matched';
elseif strcmp(row.method,'cwt') && isequal(failed,{'significance_ratio'}) && ...
        isfield(row.comparisons,'significance_ratio_exact_quantile') && row.comparisons.significance_ratio_exact_quantile.pass
    status='matched_with_quantile_approximation';
else, status='discrepancy'; end
end
function result=compare(actual,expected,tolerance)
actual=actual(:); expected=expected(:);
result=struct('relative_max_error',[], 'tolerance',tolerance,'pass',false);
if isempty(actual) || numel(actual)~=numel(expected) || any(~isfinite(actual)) || any(~isfinite(expected)), return; end
result.relative_max_error=max(abs(actual-expected))/max(realmin,max(abs(expected)));
result.pass=result.relative_max_error<=tolerance;
end
