"""Bind the reviewed local payload to its installer; no game writes."""
from pathlib import Path
import hashlib,json

def write_manifest(package):
    files=[]
    for name in ('d3d9.dll','mahoyo-learning/local/story.dat','mahoyo-learning/local/provenance.json',
                 'mahoyo-learning/local/StudyBook.exe','mahoyo-learning/local/word-index.dat',
                 'mahoyo-learning/local/NotoSerifSC-Medium.ttf','mahoyo-learning/local/NotoSerifSC-OFL.txt',
                 'mahoyo-learning/local/shogakukan.zip'):
        raw=(package/name).read_bytes()
        files.append({'path':name,'sha256':hashlib.sha256(raw).hexdigest(),'bytes':len(raw)})
    doc={'format':'mahoyo-learning-package-v1','files':files}
    (package/'package-manifest.json').write_text(json.dumps(doc,indent=2)+'\n',encoding='utf8')
    return doc

if __name__=='__main__':
    print(json.dumps(write_manifest(Path(__file__).resolve().parent/'package')))
