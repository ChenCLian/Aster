"""Reproduce the all-value reference digest from unchanged PANGAEA archives.
Python csv/float/struct is independent of the C++ reader. No resampling or smoothing.
"""
from pathlib import Path
import csv,hashlib,json,struct
root=Path(__file__).resolve().parents[2]
folder=root/'resources/examples'
catalog=json.loads((folder/'catalog.json').read_text())
references=[]
for item in catalog['examples']:
    source=catalog['datasets'][item['dataset']]
    raw=(folder/(item['dataset']+'.tsv')).read_bytes()
    assert hashlib.sha256(raw).hexdigest()==source['sha256']
    table=list(csv.reader(raw.decode().split('*/\n',1)[1].splitlines(),delimiter='\t'))
    assert table[0]==source['columns']
    for axis in ['age','depth']:
        x,y=source[axis+'_column'],item['y_column']
        points=[(float(row[x]),float(row[y])) for row in table[1:] if row[x] and row[y]]
        digest=hashlib.sha256(b''.join(struct.pack('<dd',*point) for point in points)).hexdigest()
        references.append(dict(id=item['id'],axis=axis,count=len(points),omitted=len(table)-1-len(points),first=points[0],last=points[-1],pairs_sha256=digest,source_sha256=source['sha256']))
Path(__file__).with_name('odp_reference.json').write_text(json.dumps(references,indent=2)+'\n')
print('Verified',len(references),'full-output reference digests')
