// Included inside mod.cpp's private namespace. Observe the game's actual UV-to-screen transform.
using CreateDeviceFn=HRESULT(WINAPI*)(IDirect3D9*,UINT,D3DDEVTYPE,HWND,DWORD,D3DPRESENT_PARAMETERS*,IDirect3DDevice9**);
using EndSceneFn=HRESULT(WINAPI*)(IDirect3DDevice9*);
using PresentFn=HRESULT(WINAPI*)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*);
using StretchFn=HRESULT(WINAPI*)(IDirect3DDevice9*,IDirect3DSurface9*,const RECT*,IDirect3DSurface9*,const RECT*,D3DTEXTUREFILTERTYPE);
using ResetFn=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);
using DrawUPFn=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,const void*,UINT);
using DrawIUPFn=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT,UINT,const void*,D3DFORMAT,const void*,UINT);
using DrawFn=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT);
using DrawIFn=HRESULT(WINAPI*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT);
using CreateVBFn=HRESULT(WINAPI*)(IDirect3DDevice9*,UINT,DWORD,DWORD,D3DPOOL,IDirect3DVertexBuffer9**,HANDLE*);
using VBLockFn=HRESULT(WINAPI*)(IDirect3DVertexBuffer9*,UINT,UINT,void**,DWORD);
using VBUnlockFn=HRESULT(WINAPI*)(IDirect3DVertexBuffer9*);
using UpdateTextureFn=HRESULT(WINAPI*)(IDirect3DDevice9*,IDirect3DBaseTexture9*,IDirect3DBaseTexture9*);
CreateDeviceFn originalCreateDevice{};EndSceneFn originalEndScene{};ResetFn originalReset{};
PresentFn originalPresent{};StretchFn originalStretch{};
DrawUPFn originalDrawUP{};DrawIUPFn originalDrawIUP{};DrawFn originalDraw{};DrawIFn originalDrawI{};
CreateVBFn originalCreateVB{};VBLockFn originalVBLock{};VBUnlockFn originalVBUnlock{};
UpdateTextureFn originalUpdateTexture{};
struct VBData{std::vector<uint8_t> bytes;void* locked{};UINT offset{},size{};uint64_t used{};};
std::map<IDirect3DVertexBuffer9*,VBData> vertexBuffers;
IDirect3DTexture9*tooltipTexture{};int tooltipW{},tooltipH{};std::wstring tooltipKey;
bool insideOverlay=false;
uint64_t debugOverlayCalls=0;int debugHoverStart=-1;HRESULT debugQuadHR=0,debugBeginHR=0;
struct PointUV{float x{},y{},u{},v{};};
std::vector<std::string> debugDraws,debugCompleteDraws;uint64_t debugCompleteFrame=0,drawSequence=0;bool debugDrawsTruncated=false,debugCompleteTruncated=false;
struct DVertex{float x,y,z,rhw;D3DCOLOR color;float u,v;};
std::map<IDirect3DPixelShader9*,DWORD> shaderSamplers;
void clearShaderCache(){for(auto [shader,_]:shaderSamplers)shader->Release();shaderSamplers.clear();}
DWORD activeSamplers(IDirect3DDevice9*d){
    IDirect3DPixelShader9*shader{};d->GetPixelShader(&shader);
    if(shader){auto found=shaderSamplers.find(shader);if(found!=shaderSamplers.end()){shader->Release();return found->second;}
        UINT size=0;DWORD mask=0;if(SUCCEEDED(shader->GetFunction(nullptr,&size))&&size>=8&&size<=65536){std::vector<DWORD>code((size+3)/4);if(SUCCEEDED(shader->GetFunction(code.data(),&size))){for(size_t i=1;i<code.size();){DWORD op=code[i]&0xffff;if(op==0xffff)break;if(op==0xfffe){i+=1+((code[i]>>16)&0x7fff);continue;}UINT n=(code[i]>>24)&15;if(op==31&&n>=2&&i+2<code.size()){DWORD reg=code[i+2];UINT type=((reg>>28)&7)|((reg>>8)&0x18),index=reg&0x7ff;if(type==10&&index<16)mask|=1u<<index;}if(!n)break;i+=1+n;}}}
        // Shader model 1 has implicit texture registers.
        if(!mask)mask=1;
        // Own the reference returned by GetPixelShader: pointer reuse cannot
        // inherit a different shader's sampler mask. Bound and reset the cache.
        if(shaderSamplers.size()>=128)clearShaderCache();shaderSamplers[shader]=mask;return mask;
    }
    DWORD mask=0;for(UINT stage=0;stage<4;stage++){DWORD op=D3DTOP_DISABLE;d->GetTextureStageState(stage,D3DTSS_COLOROP,&op);if(op==D3DTOP_DISABLE)break;mask|=1u<<stage;}return mask;
}
Affine compose(const Affine&m,const Affine&c){return {m.a*c.a+m.b*c.d,m.a*c.b+m.b*c.e,m.a*c.c+m.b*c.f+m.c,m.d*c.a+m.e*c.d,m.d*c.b+m.e*c.e,m.d*c.c+m.e*c.f+m.f,true};}
void surfaceMapping(Layer&l,IDirect3DSurface9*surface,Affine m,bool final,UINT alpha=255){
    // A later decorative thumbnail can sample the same intermediate texture.
    // Keep the main readable instance, rather than replacing it with the icon.
    auto prior=l.surfaces.find(surface);
    auto area=[](const Affine&v){return fabsf(v.a*v.e-v.b*v.d);};
    if(prior!=l.surfaces.end()&&prior->second.second==frameNo&&area(prior->second.first)>area(m))return;
    if(prior!=l.surfaces.end()&&prior->second.second==frameNo&&area(prior->second.first)==area(m))alpha=std::max(alpha,l.surfaceAlpha[surface]);
    l.surfaces[surface]={m,frameNo};l.surfaceAlpha[surface]=alpha;l.lastUsed=frameNo;if(final){l.mapping=m;l.drawn=frameNo;l.drawnAlpha=alpha;}
    static const GUID textureIID={0x85c31227,0x3de5,0x4f00,{0x9b,0x3a,0xf1,0x1a,0xc3,0x8c,0x18,0xb5}};
    IDirect3DTexture9*texture{};
    if(SUCCEEDED(surface->GetContainer(textureIID,(void**)&texture))&&texture){D3DSURFACE_DESC desc{};if(SUCCEEDED(texture->GetLevelDesc(0,&desc))&&desc.Width&&desc.Height){m.a/=desc.Width;m.b/=desc.Width;m.c/=desc.Width;m.d/=desc.Height;m.e/=desc.Height;m.f/=desc.Height;l.composites[texture]={m,frameNo};l.compositeAlpha[texture]=alpha;compositeLayers[texture]=l.native;}texture->Release();}
}
mh::Box mapBox(const Affine&m,mh::Box b,int tw,int th){
    float minx=1e9f,miny=1e9f,maxx=-1e9f,maxy=-1e9f;
    for(int y:{b.t,b.b})for(int x:{b.l,b.r}){float u=float(x)/tw,v=float(y)/th;float px=m.a*u+m.b*v+m.c,py=m.d*u+m.e*v+m.f;minx=std::min(minx,px);maxx=std::max(maxx,px);miny=std::min(miny,py);maxy=std::max(maxy,py);}
    return {int(floorf(minx)),int(floorf(miny)),int(ceilf(maxx)),int(ceilf(maxy))};
}
bool transformVertex(IDirect3DDevice9*d,const float*p,bool rhw,PointUV&v){
    if(rhw){v.x=p[0];v.y=p[1];return std::isfinite(v.x)&&std::isfinite(v.y);}
    D3DMATRIX matrices[3];if(FAILED(d->GetTransform(D3DTS_WORLD,&matrices[0]))||FAILED(d->GetTransform(D3DTS_VIEW,&matrices[1]))||FAILED(d->GetTransform(D3DTS_PROJECTION,&matrices[2])))return false;
    float q[4]={p[0],p[1],p[2],1};for(auto&m:matrices){float r[4]={};for(int j=0;j<4;j++)for(int k=0;k<4;k++)r[j]+=q[k]*m.m[k][j];memcpy(q,r,sizeof(q));}
    if(fabsf(q[3])<1e-6f)return false;D3DVIEWPORT9 vp{};d->GetViewport(&vp);v.x=vp.X+(q[0]/q[3]+1)*vp.Width/2;v.y=vp.Y+(1-q[1]/q[3])*vp.Height/2;return std::isfinite(v.x)&&std::isfinite(v.y);
}
bool occludesText(const std::vector<mh::Box>&rows,mh::Box draw){
    long long total=0,covered=0;bool coveredLine=false;
    int drawW=std::max(0,draw.r-draw.l),drawH=std::max(0,draw.b-draw.t);
    for(auto r:rows){int h=std::max(0,r.b-r.t);long long area=(long long)std::max(0,r.r-r.l)*h;
        long long overlap=(long long)std::max(0,std::min(r.r,draw.r)-std::max(r.l,draw.l))*std::max(0,std::min(r.b,draw.b)-std::max(r.t,draw.t));total+=area;covered+=overlap;
        // A centered confirmation panel can cover most of one old paragraph
        // but less than 70% of a long page. Require a genuinely large quad to
        // avoid treating the small next-page icon or an underline as a modal.
        if(area>0&&drawW>=4*h&&drawH>=2*h&&overlap*2>=area)coveredLine=true;
    }
    return coveredLine||(total>0&&covered*10>=total*7);
}
// The game's fixed-function text composite keeps drawing on a cleared page.
// Its stage alpha multiplies by D3DRS_TEXTUREFACTOR, whose high byte drops
// to zero. Track known stage values; -1 denotes a texture-dependent value.
int stageAlpha(DWORD operation,int first,int second){
    switch(operation){
        case D3DTOP_SELECTARG1:return first;
        case D3DTOP_SELECTARG2:return second;
        case D3DTOP_MODULATE:case D3DTOP_MODULATE2X:case D3DTOP_MODULATE4X:{
            if(!first || !second)return 0;
            if(first<0 || second<0)return -1;
            const int scale=operation==D3DTOP_MODULATE?1:operation==D3DTOP_MODULATE2X?2:4;
            return std::min(255,(first*second*scale+127)/255);
        }
        default:return -1;
    }
}
bool fixedAlphaIsZero(IDirect3DDevice9* d,UINT alphaMin,UINT alphaMax){
    IDirect3DPixelShader9* shader{};
    if(FAILED(d->GetPixelShader(&shader)))return false;
    if(shader){shader->Release();return false;}
    DWORD factor=0;
    if(FAILED(d->GetRenderState(D3DRS_TEXTUREFACTOR,&factor)))return false;
    int diffuse=alphaMin==alphaMax?static_cast<int>(alphaMax):-1;
    int current=diffuse;
    for(DWORD stage=0;stage<8;++stage){
        DWORD colorOp=0,op=0,first=0,second=0,result=0;
        if(FAILED(d->GetTextureStageState(stage,D3DTSS_COLOROP,&colorOp)))return false;
        if(colorOp==D3DTOP_DISABLE)break;
        if(FAILED(d->GetTextureStageState(stage,D3DTSS_ALPHAOP,&op)) ||
           FAILED(d->GetTextureStageState(stage,D3DTSS_RESULTARG,&result)) ||
           FAILED(d->GetTextureStageState(stage,D3DTSS_ALPHAARG1,&first)) ||
           FAILED(d->GetTextureStageState(stage,D3DTSS_ALPHAARG2,&second)))return false;
        if(result!=D3DTA_CURRENT)continue;
        auto value=[&](DWORD arg){
            int alpha=-1;
            switch(arg&D3DTA_SELECTMASK){
                case D3DTA_TFACTOR:alpha=int(factor>>24);break;
                case D3DTA_DIFFUSE:alpha=diffuse;break;
                case D3DTA_CURRENT:alpha=current;break;
            }
            return (arg&D3DTA_COMPLEMENT) && alpha>=0?255-alpha:alpha;
        };
        current=stageAlpha(op,value(first),value(second));
    }
    return current==0;
}
void captureVertices(IDirect3DDevice9*d,const void*data,UINT stride,UINT count){
    if(insideOverlay||!data||stride<16||stride>256||count<3||!readable(data,(size_t)stride*std::min(count,8u)))return;
    StudyFpScope fp;
    std::lock_guard<std::recursive_mutex>guard(stateMutex);if(gpuLayers.empty())return;drawSequence++;
    DWORD samplers=activeSamplers(d);
    void*owner=nullptr;UINT sourceStage=0,sourceAlpha=255;Affine sourceTransform{1,0,0,0,1,0,true};
    for(UINT stage=0;stage<4;stage++){if(!(samplers&(1u<<stage)))continue;IDirect3DBaseTexture9*base{};if(SUCCEEDED(d->GetTexture(stage,&base))&&base){auto it=gpuLayers.find((IDirect3DTexture9*)base);base->Release();if(it!=gpuLayers.end()){owner=it->second;sourceStage=stage;break;}}}
    if(!owner)for(UINT stage=0;stage<4;stage++){if(!(samplers&(1u<<stage)))continue;IDirect3DBaseTexture9*base{};if(SUCCEEDED(d->GetTexture(stage,&base))&&base){auto it=compositeLayers.find((IDirect3DTexture9*)base);if(it!=compositeLayers.end()){auto lit=layers.find(it->second);if(lit!=layers.end()){auto c=lit->second.composites.find((IDirect3DTexture9*)base);if(c!=lit->second.composites.end()&&c->second.second==frameNo){owner=it->second;sourceStage=stage;sourceTransform=c->second.first;sourceAlpha=lit->second.compositeAlpha[(IDirect3DTexture9*)base];}}}base->Release();if(owner)break;}}
    auto layer=layers.find(owner);if(layer==layers.end()||layer->second.pageAnnotations.empty())owner=nullptr;
    // Retain the surface reference throughout observation; GetContainer/GetDesc
    // must not run on a released temporary interface.
    IDirect3DSurface9*target{},*back{};if(FAILED(d->GetRenderTarget(0,&target))||!target)return;bool direct=SUCCEEDED(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back))&&target==back;
    struct SurfaceRef{IDirect3DSurface9*p;~SurfaceRef(){p->Release();}}targetRef{target};auto targetIdentity=target;if(back)back->Release();
    DWORD fvf{};d->GetFVF(&fvf);unsigned posOffset=0,uvOffset=0,colorOffset=0;bool rhw=false,formatOK=false,hasColor=false;
    if(fvf){DWORD position=fvf&D3DFVF_POSITION_MASK;rhw=position==D3DFVF_XYZRHW;unsigned floats=rhw?4:position==D3DFVF_XYZ?3:0;UINT texCount=(fvf&D3DFVF_TEXCOUNT_MASK)>>D3DFVF_TEXCOUNT_SHIFT;DWORD coordIndex=0;d->GetTextureStageState(sourceStage,D3DTSS_TEXCOORDINDEX,&coordIndex);coordIndex&=0xffff;if(coordIndex>=texCount)coordIndex=0;if(floats){uvOffset=floats*4;if(fvf&D3DFVF_NORMAL)uvOffset+=12;if(fvf&D3DFVF_PSIZE)uvOffset+=4;if(fvf&D3DFVF_DIFFUSE){hasColor=true;colorOffset=uvOffset;uvOffset+=4;}if(fvf&D3DFVF_SPECULAR)uvOffset+=4;for(UINT j=0;j<coordIndex;j++){UINT code=(fvf>>(16+j*2))&3;uvOffset+=code==0?8:code==1?12:code==2?16:4;}formatOK=texCount>0||!owner;}}
    else{
        IDirect3DVertexDeclaration9*decl{};if(SUCCEEDED(d->GetVertexDeclaration(&decl))&&decl){D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH+1];UINT n=MAXD3DDECLLENGTH+1;if(SUCCEEDED(decl->GetDeclaration(elements,&n))){bool pos=false,tex=false;for(UINT i=0;i<n;i++){auto e=elements[i];if(e.Stream!=0)continue;if(e.Usage==D3DDECLUSAGE_POSITIONT||e.Usage==D3DDECLUSAGE_POSITION){posOffset=e.Offset;rhw=e.Usage==D3DDECLUSAGE_POSITIONT;pos=e.Type==D3DDECLTYPE_FLOAT3||e.Type==D3DDECLTYPE_FLOAT4;}if(e.Usage==D3DDECLUSAGE_TEXCOORD&&e.UsageIndex==0&&e.Type==D3DDECLTYPE_FLOAT2){uvOffset=e.Offset;tex=true;}if(e.Usage==D3DDECLUSAGE_COLOR&&e.UsageIndex==0&&e.Type==D3DDECLTYPE_D3DCOLOR){colorOffset=e.Offset;hasColor=true;}}formatOK=pos&&(tex||!owner);}decl->Release();}
    }
    if(!formatOK||(owner&&uvOffset+8>stride)||posOffset+12>stride)return;
    PointUV v[8];UINT n=std::min(count,8u),alphaMin=255,alphaMax=0;float minx=1e9f,miny=1e9f,maxx=-1e9f,maxy=-1e9f;
    for(UINT i=0;i<n;i++){auto p=(const uint8_t*)data+i*stride;float xyz[4]={};memcpy(xyz,p+posOffset,std::min(16u,stride-posOffset));if(!transformVertex(d,xyz,rhw,v[i]))return;if(uvOffset+8<=stride)memcpy(&v[i].u,p+uvOffset,8);minx=std::min(minx,v[i].x);miny=std::min(miny,v[i].y);maxx=std::max(maxx,v[i].x);maxy=std::max(maxy,v[i].y);UINT alpha=255;if(hasColor&&colorOffset+4<=stride)alpha=read<DWORD>(p,colorOffset)>>24;alphaMin=std::min(alphaMin,alpha);alphaMax=std::max(alphaMax,alpha);}
    if(!std::isfinite(minx)||!std::isfinite(maxx)||minx< -100000||maxx>100000||miny< -100000||maxy>100000)return;
    mh::Box drawBox{(int)floorf(minx),(int)floorf(miny),(int)ceilf(maxx),(int)ceilf(maxy)};DWORD blending=0;d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blending);
    if(debugDrawCapture){if(debugDraws.size()<384){std::ostringstream o;D3DSURFACE_DESC td{};target->GetDesc(&td);o<<"{\"frame\":"<<frameNo<<",\"sequence\":"<<drawSequence<<",\"stage\":"<<sourceStage<<",\"samplers\":"<<samplers<<",\"knownText\":"<<(owner?"true":"false")<<",\"direct\":"<<(direct?"true":"false")<<",\"target\":\""<<target<<"\",\"size\":["<<td.Width<<','<<td.Height<<"],\"box\":["<<drawBox.l<<','<<drawBox.t<<','<<drawBox.r<<','<<drawBox.b<<"],\"alpha\":["<<alphaMin<<','<<alphaMax<<"],\"blend\":"<<blending<<'}';debugDraws.push_back(o.str());}else debugDrawsTruncated=true;}
    if(!owner){
        // A menu/fade quad drawn after the text into the same surface hides the
        // interactive study layer for this frame. This is deliberately geometric
        // and exposed in diagnostics; it does not guess an engine menu address.
        if(alphaMax>0||!blending)for(auto&[_,l]:layers){auto prior=l.surfaces.find(target);if(prior==l.surfaces.end()||prior->second.second!=frameNo||!l.gpuW||!l.gpuH)continue;std::vector<mh::Box> rows;for(auto&a:l.pageAnnotations)for(auto b:a.body)rows.push_back(mapBox(prior->second.first,b,l.gpuW,l.gpuH));if(occludesText(rows,drawBox))l.occludedFrame=frameNo;}
        return;
    }
    for(UINT i=1;i+1<n;i++){
        auto p=v[0],q=v[i],r=v[i+1];float du=q.u-p.u,dv=q.v-p.v,eu=r.u-p.u,ev=r.v-p.v,det=du*ev-eu*dv;if(fabsf(det)<1e-8f)continue;
        Affine m;m.a=((q.x-p.x)*ev-(r.x-p.x)*dv)/det;m.b=(du*(r.x-p.x)-eu*(q.x-p.x))/det;m.c=p.x-m.a*p.u-m.b*p.v;m.d=((q.y-p.y)*ev-(r.y-p.y)*dv)/det;m.e=(du*(r.y-p.y)-eu*(q.y-p.y))/det;m.f=p.y-m.d*p.u-m.e*p.v;m.valid=true;
        auto&l=layer->second;if(fabsf(m.a*m.e-m.b*m.d)<1e-4f)return;
        m=compose(m,sourceTransform);
        UINT alpha=blending?alphaMax:255;
        if(blending && fixedAlphaIsZero(d,alphaMin,alphaMax))alpha=0;
        surfaceMapping(l,targetIdentity,m,direct,(alpha*sourceAlpha+127)/255);return;
    }
}
UINT vertexCount(D3DPRIMITIVETYPE t,UINT p){switch(t){case D3DPT_TRIANGLELIST:return p*3;case D3DPT_TRIANGLESTRIP:case D3DPT_TRIANGLEFAN:return p+2;default:return 0;}}
HRESULT WINAPI onDrawUP(IDirect3DDevice9*d,D3DPRIMITIVETYPE t,UINT p,const void*v,UINT stride){if(!insideOverlay)captureVertices(d,v,stride,vertexCount(t,p));return originalDrawUP(d,t,p,v,stride);}
HRESULT WINAPI onDrawIUP(IDirect3DDevice9*d,D3DPRIMITIVETYPE t,UINT min,UINT n,UINT p,const void*idx,D3DFORMAT format,const void*v,UINT stride){if(!insideOverlay)captureVertices(d,v,stride,n);return originalDrawIUP(d,t,min,n,p,idx,format,v,stride);}
void captureBuffer(IDirect3DDevice9*d,UINT start,UINT n){
    if(insideOverlay||n<3||n>100000)return;
    std::lock_guard<std::recursive_mutex>guard(stateMutex);
    if(gpuLayers.empty())return;
    IDirect3DVertexBuffer9*vb{};UINT offset{},stride{};if(FAILED(d->GetStreamSource(0,&vb,&offset,&stride))||!vb||stride==0){if(vb)vb->Release();return;}
    uint64_t bytes=(uint64_t)stride*std::min(n,8u),at=(uint64_t)offset+(uint64_t)start*stride;
    auto it=vertexBuffers.find(vb);if(it!=vertexBuffers.end()&&at+bytes<=it->second.bytes.size()){it->second.used=frameNo;captureVertices(d,it->second.bytes.data()+at,stride,n);}vb->Release();
}
constexpr size_t vbEntryLimit=512, vbShadowByteLimit=32u*1024*1024;
void trackVertexBuffer(IDirect3DVertexBuffer9*vb,UINT size){
    if(!size||size>8u*1024*1024||vertexBuffers.count(vb))return;
    size_t total=size;for(const auto& item:vertexBuffers)total+=item.second.bytes.size();
    while(vertexBuffers.size()>=vbEntryLimit||total>vbShadowByteLimit){
        auto oldest=vertexBuffers.end();
        for(auto it=vertexBuffers.begin();it!=vertexBuffers.end();++it){
            const auto& s=it->second;
            // Keep locked shadows and the current/recent draw working set across Reset.
            if(s.locked||frameNo-s.used<=2)continue;
            if(oldest==vertexBuffers.end()||s.used<oldest->second.used)oldest=it;
        }
        if(oldest==vertexBuffers.end())return; // No safe victim; a later Lock may retry.
        total-=oldest->second.bytes.size();auto old=oldest->first;vertexBuffers.erase(oldest);old->Release();
    }
    VBData data;data.bytes.resize(size);data.used=frameNo;vertexBuffers.emplace(vb,std::move(data));vb->AddRef();
}
HRESULT WINAPI onVBLock(IDirect3DVertexBuffer9*vb,UINT offset,UINT size,void**out,DWORD flags){
    HRESULT hr=originalVBLock(vb,offset,size,out,flags);if(SUCCEEDED(hr)&&out&&*out){std::lock_guard<std::recursive_mutex>guard(stateMutex);if(!vertexBuffers.count(vb)){D3DVERTEXBUFFER_DESC desc{};if(SUCCEEDED(vb->GetDesc(&desc)))trackVertexBuffer(vb,desc.Size);}auto it=vertexBuffers.find(vb);if(it!=vertexBuffers.end()){auto&s=it->second;if(offset<=s.bytes.size()&&(!size||size<=s.bytes.size()-offset)){s.locked=*out;s.offset=offset;s.size=size?size:(UINT)(s.bytes.size()-offset);s.used=frameNo;}}}return hr;
}
HRESULT WINAPI onVBUnlock(IDirect3DVertexBuffer9*vb){
    {std::lock_guard<std::recursive_mutex>guard(stateMutex);auto it=vertexBuffers.find(vb);if(it!=vertexBuffers.end()){auto&s=it->second;if(s.locked&&(uint64_t)s.offset+s.size<=s.bytes.size()&&readable(s.locked,s.size))memcpy(s.bytes.data()+s.offset,s.locked,s.size);s.locked=nullptr;}}
    return originalVBUnlock(vb);
}
HRESULT WINAPI onCreateVB(IDirect3DDevice9*d,UINT size,DWORD usage,DWORD fvf,D3DPOOL pool,IDirect3DVertexBuffer9**out,HANDLE*shared){
    HRESULT hr=originalCreateVB(d,size,usage,fvf,pool,out,shared);if(SUCCEEDED(hr)&&out&&*out&&size<=8*1024*1024){std::lock_guard<std::recursive_mutex>guard(stateMutex);trackVertexBuffer(*out,size);auto v=*(void***)*out;if(!originalVBLock&&MH_CreateHook(v[11],(void*)onVBLock,(void**)&originalVBLock)==MH_OK)MH_EnableHook(v[11]);if(!originalVBUnlock&&MH_CreateHook(v[12],(void*)onVBUnlock,(void**)&originalVBUnlock)==MH_OK)MH_EnableHook(v[12]);}return hr;
}
HRESULT WINAPI onDraw(IDirect3DDevice9*d,D3DPRIMITIVETYPE t,UINT start,UINT p){captureBuffer(d,start,vertexCount(t,p));return originalDraw(d,t,start,p);}
HRESULT WINAPI onDrawI(IDirect3DDevice9*d,D3DPRIMITIVETYPE t,INT base,UINT min,UINT n,UINT start,UINT p){if(base>=0)captureBuffer(d,(UINT)base+min,n);return originalDrawI(d,t,base,min,n,start,p);}
HRESULT WINAPI onUpdateTexture(IDirect3DDevice9*d,IDirect3DBaseTexture9*source,IDirect3DBaseTexture9*destination){
    HRESULT hr=originalUpdateTexture(d,source,destination);if(SUCCEEDED(hr)&&destination&&destination->GetType()==D3DRTYPE_TEXTURE){std::lock_guard<std::recursive_mutex>guard(stateMutex);auto it=gpuLayers.find((IDirect3DTexture9*)source);if(it!=gpuLayers.end()){auto layer=layers.find(it->second);if(layer!=layers.end())rememberTexture(layer->second,(IDirect3DTexture9*)destination);}}return hr;
}
HRESULT WINAPI onStretch(IDirect3DDevice9*d,IDirect3DSurface9*source,const RECT*sourceRect,IDirect3DSurface9*destination,const RECT*destinationRect,D3DTEXTUREFILTERTYPE filter){
    HRESULT hr=originalStretch(d,source,sourceRect,destination,destinationRect,filter);if(FAILED(hr))return hr;
    StudyFpScope fp;
    std::lock_guard<std::recursive_mutex>guard(stateMutex);D3DSURFACE_DESC sd{},dd{};if(FAILED(source->GetDesc(&sd))||FAILED(destination->GetDesc(&dd)))return hr;
    RECT s=sourceRect?*sourceRect:RECT{0,0,(LONG)sd.Width,(LONG)sd.Height},t=destinationRect?*destinationRect:RECT{0,0,(LONG)dd.Width,(LONG)dd.Height};
    if(s.right<=s.left||s.bottom<=s.top)return hr;float sx=float(t.right-t.left)/(s.right-s.left),sy=float(t.bottom-t.top)/(s.bottom-s.top);
    IDirect3DSurface9*back{};d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back);bool final=back==destination;if(back)back->Release();
    for(auto&[_,l]:layers){auto it=l.surfaces.find(source);if(it==l.surfaces.end()||it->second.second!=frameNo)continue;auto m=it->second.first;m.a*=sx;m.b*=sx;m.c=(m.c-s.left)*sx+t.left;m.d*=sy;m.e*=sy;m.f=(m.f-s.top)*sy+t.top;surfaceMapping(l,destination,m,final,l.surfaceAlpha[source]);}
    return hr;
}
void texturedQuad(IDirect3DDevice9*d,float x,float y,float w,float h,D3DCOLOR c,IDirect3DTexture9*tex,float endU,float startU=0){
    StudyFpScope fp;
    DVertex v[4]={{x-.5f,y-.5f,0,1,c,startU,0},{x+w-.5f,y-.5f,0,1,c,endU,0},{x-.5f,y+h-.5f,0,1,c,startU,1},{x+w-.5f,y+h-.5f,0,1,c,endU,1}};d->SetTexture(0,tex);d->SetTextureStageState(0,D3DTSS_COLOROP,tex?D3DTOP_MODULATE:D3DTOP_SELECTARG2);d->SetTextureStageState(0,D3DTSS_ALPHAOP,tex?D3DTOP_MODULATE:D3DTOP_SELECTARG2);debugQuadHR=originalDrawUP(d,D3DPT_TRIANGLESTRIP,2,v,sizeof(DVertex));
}
void quad(IDirect3DDevice9*d,float x,float y,float w,float h,D3DCOLOR c,IDirect3DTexture9*tex=nullptr){
    texturedQuad(d,x,y,w,h,c,tex,1.f);
}
void releaseTooltip(){if(tooltipTexture){tooltipTexture->Release();tooltipTexture=nullptr;}tooltipKey.clear();tooltipW=tooltipH=0;}
bool readingBubble(const Word& w){return w.level==L"补注"&&w.definition.empty();}
mh::Box editInCard{},rangeInCard{},saveInCard{};
std::wstring wordActionLabel(const Word& w){
    if(w.level==L"生词本"||w.level==L"已掌握")return L"编辑释义";
    std::wistringstream lines(w.definition);std::wstring line;
    while(std::getline(lines,line))if(!line.empty()&&line.find(L"辞書形：")!=0&&line.find(L"原作注音：")!=0)return L"加入生词本";
    return L"补释义";
}
bool setTooltip(IDirect3DDevice9*d,const Word&w,int scale,mh::CardContent content){
    const bool actions=content==mh::CardContent::Word;
    auto key=w.term+L"\n"+w.reading+L"\n"+w.pos+L"\n"+w.definition+L"\n"+w.level+L"\n"+w.pitch+L"\n"+std::to_wstring(scale)+(actions?L"+":L"");if(key==tooltipKey&&tooltipTexture)return true;
    releaseTooltip();
    const auto primary=actions?wordActionLabel(w):L"";
    const bool compact=readingBubble(w);
    auto r=compact?mh::makeReadingBubble(w.term,w.reading,scale):
        mh::makeTooltip(w.term,w.reading,w.pos,w.definition,w.level==L"补注"?L"":w.level,w.pitch,420*scale/100,scale,content,primary);
    if(r.w<1||r.h<1||r.pixels.empty())return false;
    editInCard={};rangeInCard={};saveInCard={};
    if(actions)r=mh::withWordActions(r,primary,scale,editInCard,rangeInCard,saveInCard,compact);
    mh::roundCardCorners(r,scale);
    if(FAILED(d->CreateTexture(r.w,r.h,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&tooltipTexture,nullptr)))return false;
    D3DLOCKED_RECT lock{};if(FAILED(tooltipTexture->LockRect(0,&lock,nullptr,0))){releaseTooltip();return false;}
    for(int y=0;y<r.h;y++)memcpy((uint8_t*)lock.pBits+y*lock.Pitch,r.pixels.data()+y*r.w,r.w*4);tooltipTexture->UnlockRect(0);tooltipW=r.w;tooltipH=r.h;tooltipKey=key;return true;
}
// Sample the actual window key events. Frame polling can miss a short press
// when the novel waits on a stationary page or the renderer loses focus.
#include "reading_controls.inc"
#include "reading_orb.inc"
#include "reading_buttons.inc"
constexpr wchar_t originalProcProperty[]=L"MahoyoYomibito.OriginalWindowProc";
LRESULT CALLBACK studyWindowMessage(HWND window,UINT message,WPARAM key,LPARAM info){
    const auto previous=reinterpret_cast<WNDPROC>(GetPropW(window,originalProcProperty));
    if(message==WM_KEYDOWN&&key==VK_ESCAPE&&choosingWord.exchange(false)){++inputGeneration;return 0;}
    static const UINT bookClosed=RegisterWindowMessageW(L"MahoyoYomibito.BookClosed.v1");
    if(bookClosed&&message==bookClosed){guardReaderReturn(window);return 0;}
    if(readerGuardActive()&&(message==WM_LBUTTONDOWN||message==WM_LBUTTONUP||message==WM_LBUTTONDBLCLK)){
        if(GetForegroundWindow()==window)SetForegroundWindow(readerGuardWindow());
        return 0;
    }
    static const UINT markOriginal=RegisterWindowMessageW(L"MahoyoYomibito.MarkOriginal.v1");
    if(message==markOriginal){
        const int id=int(key),a=LOWORD(info),b=HIWORD(info);
        const auto it=records.find(id);
        if(it!=records.end()&&a<b&&size_t(b)<=it->second.clean.size())runStudyAction(window,{StudyAction::SaveWord,id,{a,b}});
        return 0;
    }
    if(message==WM_SETTINGCHANGE)readerAnimations()=readAnimationSetting();
    if(message==buttonUpdateMessage()){
        if(readerGuardActive()&&GetForegroundWindow()==window)SetForegroundWindow(readerGuardWindow());
        syncReadingButtons(window);return 0;
    }
    static const UINT notesChanged=RegisterWindowMessageW(L"MahoyoYomibito.SentenceNotesChanged.v1");
    if(message==notesChanged){reloadAnnotatedIds();return 0;}
    if(message==personalMessage()||message==WM_SETFOCUS){
        if(!refreshPersonal())log("Could not reload personal words; the last valid index is retained");
        if(message==personalMessage()){reloadCollectedIds();++inputGeneration;return 0;}
    }
    if(message==WM_MOVE||message==WM_SIZE||message==WM_SHOWWINDOW||message==WM_SETFOCUS)PostMessageW(window,buttonUpdateMessage(),0,0);
    bool losingFocus=message==WM_KILLFOCUS&&(!key||reinterpret_cast<HWND>(key)!=activeOrbMenu.load())&&
        !std::any_of(buttonWindows.begin(),buttonWindows.end(),[&](const auto& b){return b->window==reinterpret_cast<HWND>(key);});
    if((message==WM_KEYDOWN || (message==WM_SYSKEYDOWN&&key==VK_F10&&!(info&(1L<<29)))) && (key==VK_F8||key==VK_F9||key==VK_F10)){
        if(!(info&(1L<<30))){
            StudyTarget target=key==VK_F9?pointedTarget():StudyTarget{key==VK_F8?StudyAction::Mode:StudyAction::Shelf};
            runStudyAction(window,target);
        }
        return 0;
    }
    if((message==WM_KEYUP||(message==WM_SYSKEYUP&&key==VK_F10&&!(info&(1L<<29))))&&(key==VK_F8||key==VK_F9||key==VK_F10))return 0;
    if(message==WM_DESTROY&&recordLookups)finishRecentWrites();
    if(message==WM_CLOSE)closingWindow=true;
    if(message==WM_LBUTTONUP || message==WM_KEYUP)closingWindow=false;
    if(losingFocus)PostMessageW(window,buttonUpdateMessage(),0,0);
    if(losingFocus||message==WM_RBUTTONDOWN||message==WM_MOUSEWHEEL||message==WM_LBUTTONDOWN)choosingWord=false;
    if(message==WM_CLOSE||losingFocus||message==WM_RBUTTONDOWN||
       message==WM_LBUTTONDOWN||message==WM_MOUSEWHEEL||message==WM_KEYDOWN){
        ++inputGeneration;exceptionOpen=-1;
    }
    if(message==WM_NCDESTROY && previous &&
       GetWindowLongPtrW(window,GWLP_WNDPROC)==reinterpret_cast<LONG_PTR>(studyWindowMessage))
        SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(previous));
    if(message==WM_NCDESTROY)RemovePropW(window,originalProcProperty);
    return previous?CallWindowProcW(previous,window,message,key,info):
                    DefWindowProcW(window,message,key,info);
}
void observeStudyKeys(HWND window){
    // The game creates its D3D device on a different thread from its own
    // window. Keep the original procedure chain on that window.
    DWORD owner=0;
    GetWindowThreadProcessId(window,&owner);
    if(owner!=GetCurrentProcessId() || !ready || GetPropW(window,originalProcProperty))return;
    auto current=reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window,GWLP_WNDPROC));
    if(!current || current==studyWindowMessage)return;
    if(!SetPropW(window,originalProcProperty,reinterpret_cast<HANDLE>(current)))return;
    auto old=SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(studyWindowMessage));
    if(!old)RemovePropW(window,originalProcProperty);
    log(old?"Sentence hotkey ready":"Sentence hotkey registration failed");
}
int hoverPriority(const Word& word){
    if(word.level==L"生词本"||word.level==L"已掌握")return word.pos==L"我的本句释义"?5:4;
    if(word.level==L"补注")return 1;
    if(word.level==L"原作"){
        for(wchar_t c:word.reading)if(c>=L'ぁ'&&c<=L'ヿ')return 2;
        return 0;
    }
    return 2;
}
bool preferredWord(const Word& candidate,const Word* current){
    if(!current)return true;
    return std::make_tuple(hoverPriority(candidate),candidate.term.size(),!candidate.definition.empty())>=
           std::make_tuple(hoverPriority(*current),current->term.size(),!current->definition.empty());
}
mh::Box placeTooltip(mh::Box anchor,int cardW,int cardH,int width,int height,
                     int gap,bool compact,const std::vector<mh::Box>& occupied){
    if(cardW>width||cardH>height||cardW<=0||cardH<=0)return {};
    const int mx=std::min(10,(width-cardW)/2),my=std::min(10,(height-cardH)/2);
    const int right=width-mx-cardW,bottom=height-my-cardH;
    const int preferredX=std::clamp((anchor.l+anchor.r-cardW)/2,mx,right);
    std::vector<mh::Box> protectedInk=occupied;protectedInk.push_back(anchor);
    // At each vertical text edge, sweep the free horizontal intervals. This
    // also finds space beside a multi-line paragraph or between short lines.
    for(int clearance:{gap,0}){
        std::set<int> heights{my,bottom,std::clamp((anchor.t+anchor.b-cardH)/2,my,bottom)};
        for(const auto& r:protectedInk){
            heights.insert(std::clamp(r.t-clearance-cardH,my,bottom));
            heights.insert(std::clamp(r.b+clearance,my,bottom));
        }
        mh::Box best{};long long minCost=LLONG_MAX;
        for(int y:heights){
            std::vector<std::pair<int,int>> blocked;
            for(const auto& r:protectedInk)if(y<r.b+clearance&&y+cardH>r.t-clearance)
                blocked.emplace_back(r.l-clearance-cardW+1,r.r+clearance-1);
            std::sort(blocked.begin(),blocked.end());
            auto consider=[&](int from,int to){
                if(from>to)return;
                const int x=std::clamp(preferredX,from,to);
                const int dx=std::max({0,anchor.l-x-cardW,x-anchor.r});
                const int dy=std::max({0,anchor.t-y-cardH,y-anchor.b});
                const int align=std::abs(2*x+cardW-anchor.l-anchor.r);
                long long cost=4LL*(dx*dx+dy*dy)+align;
                if(compact?y+cardH>anchor.t:y<anchor.b)cost+=gap;
                if(cost<minCost){minCost=cost;best={x,y,x+cardW,y+cardH};}
            };
            int from=mx;
            for(auto [l,r]:blocked){
                if(r<from)continue;
                if(l>right)break;
                consider(from,std::min(right,l-1));from=std::max(from,r+1);
                if(from>right)break;
            }
            consider(from,right);
        }
        if(best.r>best.l)return best;
    }
    // A completely occupied page still has the explicit word picker.
    return {};
}
constexpr float kPreviousCaptionOpacity=0.30f;
void drawReadingFrame(IDirect3DDevice9* d,POINT mouse,RECT client,bool active,ULONGLONG now){
    StudyFpScope fp;
    debugHoverStart=-1;
    debugOverlayCalls++;
    std::lock_guard<std::recursive_mutex>guard(stateMutex);D3DVIEWPORT9 viewport{};d->GetViewport(&viewport);
    static LookupDwell lookup;
    if(client.right<1||client.bottom<1){lookup={};return;}
    float mx=float(mouse.x)*viewport.Width/client.right,my=float(mouse.y)*viewport.Height/client.bottom;
    if(!active){mx=-100000;my=-100000;}
    const int width=int(viewport.Width),screenH=int(viewport.Height);
    const auto stage=orbArea(width,screenH);
    const int scale=std::clamp((stage.b-stage.t)*100/1080,60,150);
    const int toastScale=std::clamp(screenH*100/1080,60,130);
    auto px=[scale](int value){return (value*scale+50)/100;};
    StudyFrame controls;controls.width=width;controls.height=screenH;controls.tick=now;
    std::map<int,mh::Box> lastRow;
    std::set<int> separate;
    int pointed=-1,wordId=-1;
    const Word* hovered=nullptr;Word translated;
    mh::Box hoverBox{};
    std::vector<mh::Box> occupied;
    static ParallelFocus focus;
    FocusClause pointedClause;
    std::vector<PersonalGlyph> parallelGlyphs;
    std::vector<VisibleInk> inks;
    const bool dual=inlineTranslation.load();
    const bool picking=choosingWord.load()&&active;
    mh::Box pickMark{};
    uint64_t page=1469598103934665603ULL;
    for(auto& [ptr,layer]:layers){
        if(layer.bitmapEmpty||!layer.mapping.valid||layer.drawn!=frameNo||!layer.drawnAlpha||
           layer.occludedFrame==frameNo||!layer.gpuW||!layer.gpuH)continue;
        auto map=[&](mh::Box box){return mapBox(layer.mapping,box,layer.gpuW,layer.gpuH);};
        for(auto& annotation:layer.pageAnnotations){
            const bool old=&annotation!=&layer.pageAnnotations.back();
            int focusRow=-1;
            for(size_t row=0;row<annotation.sentenceRows.size();++row){
                const auto& [box,id]=annotation.sentenceRows[row];
                auto r=map(box);
                if(r.r<=0||r.b<=0||r.l>=width||r.t>=screenH)continue;
                lastRow[id]=r;occupied.push_back(r);
                page=(page^uint32_t(id+1))*1099511628211ULL;
                if(contains(r,mx,my)){
                    pointed=id;
                    auto visible=box;
                    if(!old&&row<annotation.visibleRight.size())visible.r=annotation.visibleRight[row];
                    if(contains(map(visible),mx,my))focusRow=id;
                }
                if(annotation.companionCaption&&(!annotation.readingInk||
                    std::none_of(annotation.readingInk->begin(),annotation.readingInk->end(),
                        [recordId=id](const ReadingInk& ink){return ink.recordId==recordId;})))separate.insert(id);
                if(picking)controls.buttons.push_back({r,{StudyAction::PickLine,id},L"点词取词"});
            }
            for(const auto& box:annotation.authorMarks)occupied.push_back(map(box));
            if(picking)for(auto g:annotation.personalGlyphs){
                g.box=map(g.box);controls.glyphs.push_back(g);
                if(contains(g.box,mx,my))pickMark=g.box;
            }
            if(annotation.readingInk)for(const auto& ink:*annotation.readingInk){
                const auto& p=ink.raster;
                auto r=map({p.x,p.y,p.x+p.image.w,p.y+p.image.h});occupied.push_back(r);
                float progress=1.f;int nearest=INT_MAX;
                if(!old&&ink.sentenceRow>=0&&size_t(ink.sentenceRow)<annotation.visibleRight.size()){
                    const auto row=annotation.sentenceRows[ink.sentenceRow].first;
                    const int shown=annotation.visibleRight[ink.sentenceRow]-row.l;
                    progress=robinEpisode(ink.recordId)?(shown>0?1.f:0.f):
                        std::clamp(float(shown)/std::max(1,row.r-row.l),0.f,1.f);
                }else if(!old)for(size_t i=0;i<annotation.sentenceRows.size()&&i<annotation.visibleRight.size();++i){
                    auto [box,id]=annotation.sentenceRows[i];
                    int distance=std::abs(box.t-p.y-p.image.h);
                    if(id!=ink.recordId||distance>=nearest)continue;
                    nearest=distance;
                    progress=std::clamp(float(annotation.visibleRight[i]-p.x)/std::max(1,p.image.w),0.f,1.f);
                    if(annotation.visibleRight[i]>=box.r)progress=1.f;
                }
                inks.push_back({annotation.readingInk,&ink,r,old&&dual&&!robinEpisode(ink.recordId)?kPreviousCaptionOpacity:1.f,layer.drawnAlpha,progress});
            }
            if(parallelHighlight&&active&&!picking&&(pointed>=0||!focus.fades.empty())&&annotation.readingInk){
                // Without a reliable internal anchor, the record still supplies an exact sentence pair.
                if(focusRow>=0&&std::any_of(annotation.readingInk->begin(),annotation.readingInk->end(),
                    [&](const ReadingInk& ink){return ink.recordId==focusRow;}))pointedClause={focusRow,{}};
                for(const auto& g:annotation.personalGlyphs){
                    if(g.id!=pointed&&std::none_of(focus.fades.begin(),focus.fades.end(),[&](const auto& f){return f.clause.id==g.id;}))continue;
                    auto r=map(g.box);
                    bool under=contains(r,mx,my);
                    auto glyph=g;glyph.box=r;parallelGlyphs.push_back(glyph);
                    if(!under||g.id!=focusRow)continue;
                    for(const auto& ink:*annotation.readingInk){
                        auto anchors=ink.raster.slice.anchors;
                        if(ink.recordId!=g.id||!anchors)continue;
                        int offset=g.start+int((mx-r.l)*(g.end-g.start)/std::max(1,r.r-r.l));
                        if(offset>=0&&mh::clauseAt(*anchors,offset,pointedClause.range))pointedClause.id=g.id;
                        break;
                    }
                }
            }
            updatePersonalBoxes(annotation);
            for(const auto& w:annotation.personalBoxes){
                auto r=map(w.box);if(r.r<0||r.l>width||r.b<0||r.t>screenH)continue;
                if(contains(r,mx,my)&&preferredWord(w.word,hovered)){hovered=&w.word;hoverBox=r;wordId=w.recordId;}
            }
            for(auto& w:annotation.words){
                auto r=map(w.box);
                if(r.r<0||r.l>width||r.b<0||r.t>screenH)continue;
                if(contains(r,mx,my)&&preferredWord(w.word,hovered)){
                    hovered=&w.word;hoverBox=r;wordId=w.recordId;
                }
            }
        }
    }
    trimInkTextures();
    const auto orb=orbBox(width,screenH);
    if(!lastRow.empty())occupied.push_back(orb);
    std::map<int,mh::Box> feathers;
    // The union of glyph boxes can be taller than the visible body glyphs.
    const int bodyHeight=MulDiv(40,stage.b-stage.t,1071);
    for(const auto& [id,row]:lastRow){
        const int reach=MulDiv(115,std::min(bodyHeight,row.b-row.t),100);
        auto feather=mh::Box{row.r,row.t,row.r+reach,row.b};
        feathers.emplace(id,feather);occupied.push_back(feather);
    }
    unsigned generation=inputGeneration.load();
    if(page!=dwell.lastPage)exceptionOpen=-1;
    static Word lastWord;
    static mh::Box lastWordBox{},wordButton{},wordCardBox{};
    static int lastWordId=-1;
    static uint64_t lastWordPage=0;
    static unsigned lastGeneration=0;
    static ULONGLONG lastWordAt=0,captionSince=0;
    static int captionId=-1;
    static mh::Box shownCard{};
    static ULONGLONG cardSince=0,cardLeft=0;
    static float fadeFrom=0;
    static bool cardShown=false;
    static int shownId=-1;
    static WordSpan shownSpan{-1,-1};
    static ULONGLONG cardDuration=0;
    const bool animate=readerAnimations().load();
    if(!active||picking||page!=dwell.lastPage||generation!=dwell.generation){
        shownCard={};cardShown=false;fadeFrom=0;captionId=-1;
    }
    StudyTarget hoveredTarget;
    if(picking){hovered=nullptr;wordId=-1;lastWordId=-1;wordCardBox={};wordButton={};}
    if(hovered)hoveredTarget={StudyAction::SaveWord,wordId,{int(hovered->start),int(hovered->end)}};
    const bool supplementalReady=supplementalDwell.update(hovered,wordId,mouse,active,page,generation,now);
    if(hovered&&hovered->level==L"补注"&&!supplementalReady){hovered=nullptr;wordId=-1;}
    // Keep the card actionable when crossing the gap from the word to its controls.
    const mh::Box bridge{std::min(lastWordBox.l,wordCardBox.l),std::min(lastWordBox.t,wordCardBox.t),
                         std::max(lastWordBox.r,wordCardBox.r),std::max(lastWordBox.b,wordCardBox.b)};
    if(hovered){
        lastWord=*hovered;lastWordId=wordId;lastWordBox=hoverBox;
        lastWordAt=now;lastWordPage=page;lastGeneration=generation;
    }else if(active&&lastWordId>=0&&lastWordPage==page&&lastGeneration==generation&&
             (contains(wordButton,mx,my)||contains(wordCardBox,mx,my)||(pointed<0&&wordCardBox.r>wordCardBox.l&&contains(bridge,mx,my)&&now>=lastWordAt&&now-lastWordAt<550))){
        hovered=&lastWord;hoverBox=lastWordBox;wordId=lastWordId;
    }else {lastWordId=-1;wordButton={};wordCardBox={};}
    // Common kanji now have quiet reading cards. They leave sentence dwell active.
    bool wordTakesPriority=hovered&&hovered->level!=L"补注";
    bool reveal=dwell.update(mouse,(!dual&&!wordTakesPriority&&!picking)?pointed:-1,page,generation,now);
    debugRevealed=reveal?pointed:-1;
    if(captionId!=debugRevealed){captionId=debugRevealed;captionSince=now;}
    if(pointedClause.id!=pointed)pointedClause={};
    focus.update(pointedClause,mouse,page,generation,now,animate,parallelHighlight&&active&&!picking);

    const float captionOpacity=motionProgress(now,captionSince,animate?150:0);
    // The explicit shortcut can save the word before its hover card appears.
    if(hovered)controls.underPointer={StudyAction::SaveWord,wordId,{int(hovered->start),int(hovered->end)}};
    else if(hoveredTarget.action==StudyAction::SaveWord)controls.underPointer=hoveredTarget;
    else if(pointed>=0)controls.underPointer={StudyAction::SaveSentence,pointed};
    struct LineEndDraw{mh::Box box;StudyTexture image;};
    std::vector<LineEndDraw> lineEndDraws;
    static int previousLineEnd=-1;
    static uint64_t lastLineEndPage=0;
    static unsigned lastLineEndGeneration=0;
    if(!active||picking||page!=lastLineEndPage||generation!=lastLineEndGeneration)previousLineEnd=-1;
    lastLineEndPage=page;lastLineEndGeneration=generation;
    int currentLineEnd=-1;
    const int featherGap=px(12),bookmarkSize=px(26);
    auto fits=[&](mh::Box box){
        return box.l>=stage.l&&box.t>=stage.t&&box.r<=stage.r&&box.b<=stage.b&&
            std::none_of(occupied.begin(),occupied.end(),[&](mh::Box other){return intersects(box,other);});
    };
    auto fitAtY=[&](int x,int y,int w,int h){
        if(y<stage.t||y+h>stage.b)return mh::Box{};
        const int left=std::clamp(x,stage.l,stage.r-w);
        auto boxAt=[&](int candidate){return mh::Box{candidate,y,candidate+w,y+h};};
        if(fits(boxAt(left)))return boxAt(left);
        mh::Box best{};int distance=INT_MAX;
        for(const auto& other:occupied)if(y<other.b&&other.t<y+h){
            for(int candidate:{other.l-w,other.r}){
                const int delta=std::abs(candidate-left);
                if(delta<distance&&fits(boxAt(candidate))){best=boxAt(candidate);distance=delta;}
            }
        }
        return best;
    };
    auto lineEndBox=[&](mh::Box row,int x,int w,int h){
        if(w<=0||h<=0||w>stage.r-stage.l||h>stage.b-stage.t)return mh::Box{};
        const int y=std::clamp((row.t+row.b-h)/2,stage.t,stage.b-h);
        const mh::Box right{x,y,x+w,y+h};
        if(fits(right))return right;
        // If the feather is at the edge, use open space under or above the line.
        for(int top:{row.b+featherGap,row.t-featherGap-h}){
            const auto box=fitAtY(x,top,w,h);
            if(box.r>box.l)return box;
        }
        return mh::Box{};
    };
    auto addLineEnd=[&](mh::Box box,StudyTarget target,const wchar_t* text,const StudyTexture& image){
        if(!image.texture)return;
        occupied.push_back(box);controls.buttons.push_back({box,target,text});
        lineEndDraws.push_back({box,image});
    };
    for(const auto& [id,row]:lastRow){
        const int x=feathers.at(id).r+featherGap;
        const auto bookmark=lineEndBox(row,x,bookmarkSize,bookmarkSize);
        const bool hasBookmark=bookmark.r>bookmark.l;
        const bool overBookmark=hasBookmark&&contains(bookmark,mx,my);
        const bool open=exceptionOpen==id;
        const auto labelState=open?LineEndImage::HideTranslation:LineEndImage::ShowTranslation;
        StudyTexture label;
        mh::Box translation{};
        const bool hasDetails=sentenceGlosses.count(id)!=0;
        if(separate.count(id)||hasDetails){
            label=lineEndTexture(d,labelState,scale);
            // Reserve the bookmark slot even when the pointer is away from the line.
            if(hasBookmark)occupied.push_back(bookmark);
            translation=lineEndBox(row,x,label.w,label.h);
            if(translation.r==translation.l&&hasBookmark)
                translation=lineEndBox(row,std::max(x,bookmark.r+featherGap),label.w,label.h);
            if(hasBookmark)occupied.pop_back();
        }
        const bool overTranslation=translation.r>translation.l&&contains(translation,mx,my);
        auto bridges=[&](mh::Box box){
            return box.r>box.l&&contains({std::min(row.l,box.l),std::min(row.t,box.t),
                std::max(row.r,box.r),std::max(row.b,box.b)},mx,my);
        };
        const bool lineActive=pointed==id||(pointed<0&&previousLineEnd==id&&
            (bridges(bookmark)||bridges(translation)));
        if(lineActive&&!picking)currentLineEnd=id;
        if(translation.r>translation.l&&((dual&&!hasDetails)||lineActive||overTranslation||open))
            addLineEnd(translation,{StudyAction::Translation,id},open?L"收起译文":L"查看译文",label);
        if(!picking&&hasBookmark&&(lineActive||overBookmark)){
            const bool collected=collectedIds.count(id)!=0;
            const bool annotated=collected&&annotatedIds.count(id);
            const auto state=annotated?(overBookmark?LineEndImage::AnnotatedHover:LineEndImage::Annotated):overBookmark?(collected?LineEndImage::CollectedHover:LineEndImage::BookmarkHover):
                (collected?LineEndImage::Collected:LineEndImage::Bookmark);
            addLineEnd(bookmark,{StudyAction::SaveSentence,id},L"收藏本句 · F9",lineEndTexture(d,state,scale));
            if(overBookmark){
                controls.underPointer={StudyAction::SaveSentence,id};
                const auto& tip=lineEndTexture(d,LineEndImage::Hint,scale);
                if(tip.w>0&&tip.w<=stage.r-stage.l){
                    const int tipX=(bookmark.l+bookmark.r-tip.w)/2;
                    const int tipY=bookmark.t-px(4)-tip.h;
                    const auto box=fitAtY(tipX,tipY,tip.w,tip.h);
                    if(box.r>box.l){occupied.push_back(box);lineEndDraws.push_back({box,tip});}
                }
            }
        }
    }
    previousLineEnd=currentLineEnd;
    int opened=exceptionOpen.load();
    int translationId=lastRow.count(opened)&&(separate.count(opened)||sentenceGlosses.count(opened))?
        opened:(reveal&&separate.count(pointed)?pointed:-1);
    if(translationId>=0&&(!hovered||translationId==opened)){
        translated.term=L"译文";translated.definition=detailedTranslation(records.at(translationId));
        hovered=&translated;wordId=-1;hoverBox=lastRow.at(translationId);
    }
    if(!lastRow.empty()){
        for(const auto& row:lastRow)controls.pageIds.push_back(row.first);
        controls.buttons.insert(controls.buttons.begin(),StudyButton{orb,{StudyAction::Orb},L"读本"});
    }
    std::wstring message;ULONGLONG started=0,until=0;
    {std::lock_guard<std::mutex> lock(controlsMutex);if(now>=noticeStart&&now<noticeUntil){message=shelfNotice;started=noticeStart;until=noticeUntil;}}
    if(hovered)debugHoverStart=hovered->start;
    if(!hovered&&cardShown){
        fadeFrom=motionProgress(now,cardSince,animate?cardDuration:0);
        cardLeft=now;cardShown=false;
    }
    const float fading=fadeFrom*(1.f-motionProgress(now,cardLeft,animate?80:0));
    if(!hovered||!active||picking)lookup={};
    if(inks.empty()&&!hovered&&controls.buttons.empty()&&message.empty()&&fading<=0){
        publishControls(std::move(controls));return;
    }
    IDirect3DStateBlock9*state{};if(FAILED(d->CreateStateBlock(D3DSBT_ALL,&state))||!state){lookup={};return;}state->Capture();insideOverlay=true;
    d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1);d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_LIGHTING,FALSE);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);d->SetRenderState(D3DRS_STENCILENABLE,FALSE);d->SetRenderState(D3DRS_FOGENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
    d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_MODULATE);d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);d->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_MODULATE);d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);d->SetTextureStageState(0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE);d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);d->SetTextureStageState(1,D3DTSS_ALPHAOP,D3DTOP_DISABLE);d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
    for(const auto& ink:inks)if(dual||(reveal&&ink.source->recordId==pointed))drawInk(d,ink,dual?1.f:captionOpacity,focus);
    if(dual||reveal)for(const auto& f:focus.fades){
        std::map<int,mh::Box> rows;
        for(const auto& g:parallelGlyphs){
            auto range=f.clause.range;
            if(g.id!=f.clause.id||f.clause.wholeSentence()||g.end<=int(range.jaStart)||g.start>=int(range.jaEnd))continue;
            auto box=g.box;
            int w=box.r-box.l,count=std::max(1,g.end-g.start);
            box.r=g.box.l+w*(std::min(g.end,int(range.jaEnd))-g.start)/count;
            box.l+=w*(std::max(g.start,int(range.jaStart))-g.start)/count;
            auto it=rows.find(g.line);if(it==rows.end())rows.emplace(g.line,box);else it->second=unite(it->second,box);
        }
        for(const auto& row:rows){const auto& b=row.second;
            quad(d,float(b.l),float(b.b),float(b.r-b.l),1.5f*scale/100.f,withOpacity(reader_palette::argb::Accent,.55f*f.value));
        }
    }
    if(picking&&pickMark.r>pickMark.l)quad(d,float(pickMark.l),float(pickMark.t),float(pickMark.r-pickMark.l),float(pickMark.b-pickMark.t),reader_palette::argb::PickHighlight);
    for(const auto& item:lineEndDraws)if(item.image.texture){
        const auto& box=item.box;
        quad(d,float(box.l),float(box.t),float(box.r-box.l),float(box.b-box.t),
            reader_palette::argb::TextureModulation,item.image.texture);
    }
    bool placed=false;
    if(hovered&&setTooltip(d,*hovered,scale,hovered==&translated?mh::CardContent::Translation:mh::CardContent::Word)){
        const int tooltipGap=std::max(4,12*scale/100);
        const bool compact=readingBubble(*hovered);
        auto best=placeTooltip(hoverBox,tooltipW,tooltipH,width,screenH,tooltipGap,compact,occupied);
        wordCardBox={};wordButton={};
        if(best.r>best.l){
            placed=true;shownCard=best;
            const int cardId=hovered==&translated?translationId:wordId;
            const WordSpan cardSpan=hovered==&translated?WordSpan{-1,-1}:WordSpan{int(hovered->start),int(hovered->end)};
            if(!cardShown||cardId!=shownId||cardSpan!=shownSpan){
                cardSince=now;cardDuration=cardShown?0:120;
                shownId=cardId;shownSpan=cardSpan;
            }
            if(hovered==&translated&&translationId==captionId&&exceptionOpen!=translationId){
                cardSince=captionSince;cardDuration=150;
            }
            cardShown=true;fadeFrom=0;
            const float cardOpacity=motionProgress(now,cardSince,animate?cardDuration:0);
            quad(d,float(best.l),float(best.t),float(tooltipW),float(tooltipH),
                 withOpacity(reader_palette::argb::TextureModulation,cardOpacity),tooltipTexture);
            if(wordId>=0){
                wordCardBox=best;
                auto addAction=[&](mh::Box item,StudyAction action,const std::wstring& label){
                    mh::Box box{best.l+item.l,best.t+item.t,best.l+item.r,best.t+item.b};
                    if(contains(box,mx,my))quad(d,float(box.l),float(box.t),float(box.r-box.l),float(box.b-box.t),withOpacity(reader_palette::argb::HoverTint,cardOpacity));
                    controls.buttons.push_back({box,{action,wordId,{int(hovered->start),int(hovered->end)}},label});
                    return box;
                };
                wordButton=addAction(editInCard,StudyAction::EditWord,wordActionLabel(*hovered));
                addAction(rangeInCard,StudyAction::SelectRange,L"调整词界");
                addAction(saveInCard,StudyAction::CardSentence,L"收藏原句");
            }
        }
    }
    if(recordLookups&&lookup.update(placed&&active&&!picking&&hovered!=&translated?wordId:-1,shownSpan,page,generation,now))queueRecent(wordId,shownSpan);
    if(!placed){
        cardShown=false;
        if(!hovered&&fading>0&&tooltipTexture)
            quad(d,float(shownCard.l),float(shownCard.t),float(tooltipW),float(tooltipH),
                 withOpacity(reader_palette::argb::TextureModulation,fading),tooltipTexture);
    }
    if(!message.empty()){
        const float in=motionProgress(now,started,animate?150:0);
        const float out=animate&&now>until-250?1.f-motionProgress(now,until-250,250):1.f;
        drawToast(d,message,toastScale,width,screenH-std::max(22,32*toastScale/100),std::min(in,out));
    }
    publishControls(std::move(controls));
    state->Apply();state->Release();insideOverlay=false;
}
void overlay(IDirect3DDevice9* d){
    if(!ready||!gameWindow||closingWindow)return;
    POINT mouse{};GetCursorPos(&mouse);ScreenToClient(gameWindow,&mouse);
    RECT client{};GetClientRect(gameWindow,&client);
    auto foreground=GetForegroundWindow();
    drawReadingFrame(d,mouse,client,foreground&&(foreground==gameWindow||foreground==activeReadingButton.load()),GetTickCount64());
}
HRESULT WINAPI onPresent(IDirect3DDevice9*d,const RECT*s,const RECT*t,HWND overrideWindow,const RGNDATA*dirty){
    IDirect3DSurface9*saved{},*back{},*depth{};D3DVIEWPORT9 vp{};d->GetViewport(&vp);d->GetDepthStencilSurface(&depth);
    if(SUCCEEDED(d->GetRenderTarget(0,&saved))&&SUCCEEDED(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back))&&back){
        D3DSURFACE_DESC desc{};back->GetDesc(&desc);d->SetDepthStencilSurface(nullptr);d->SetRenderTarget(0,back);D3DVIEWPORT9 full{0,0,desc.Width,desc.Height,0.f,1.f};d->SetViewport(&full);
        debugBeginHR=d->BeginScene();if(SUCCEEDED(debugBeginHR)){overlay(d);d->EndScene();}d->SetRenderTarget(0,saved);d->SetDepthStencilSurface(depth);d->SetViewport(&vp);
    }
    if(saved)saved->Release();if(back)back->Release();if(depth)depth->Release();HRESULT hr=originalPresent(d,s,t,overrideWindow,dirty);
    {std::lock_guard<std::recursive_mutex>guard(stateMutex);debugCompleteDraws.swap(debugDraws);debugDraws.clear();debugCompleteFrame=frameNo;debugCompleteTruncated=debugDrawsTruncated;debugDrawsTruncated=false;drawSequence=0;
        // Surface/composite aliases are valid only during one render pass.
        // Keeping raw identities across frames permits COM pointer reuse.
        compositeLayers.clear();for(auto&[_,l]:layers){l.surfaces.clear();l.composites.clear();l.surfaceAlpha.clear();l.compositeAlpha.clear();}frameNo++;writeRuntimeAudit();}
    return hr;
}
void forgetDefaultVertexBuffers(){for(auto it=vertexBuffers.begin();it!=vertexBuffers.end();){D3DVERTEXBUFFER_DESC desc{};if(FAILED(it->first->GetDesc(&desc))||desc.Pool==D3DPOOL_DEFAULT){it->first->Release();it=vertexBuffers.erase(it);}else{it->second.locked=nullptr;++it;}}}
void clearDeviceCache(){releaseTooltip();clearReadingTextures();clearShaderCache();forgetDefaultVertexBuffers();compositeLayers.clear();for(auto&[_,l]:layers){forgetDefaultTextures(l);l.mapping.valid=false;l.drawn=0;l.drawnAlpha=0;l.occludedFrame=0;l.surfaces.clear();l.composites.clear();l.surfaceAlpha.clear();l.compositeAlpha.clear();l.locked=nullptr;l.incoming=nullptr;}}
HRESULT WINAPI onReset(IDirect3DDevice9*d,D3DPRESENT_PARAMETERS*p){{std::lock_guard<std::recursive_mutex>guard(stateMutex);clearDeviceCache();writeRuntimeAudit(true);}return originalReset(d,p);}
void installDevice(IDirect3DDevice9*d){
    if(deviceHooked)return;auto v=*(void***)d;
    struct Hook{int slot;void*replacement;void**original;};Hook hooks[]={
        {17,(void*)onPresent,(void**)&originalPresent},{16,(void*)onReset,(void**)&originalReset},
        {83,(void*)onDrawUP,(void**)&originalDrawUP},{84,(void*)onDrawIUP,(void**)&originalDrawIUP},
        {81,(void*)onDraw,(void**)&originalDraw},{82,(void*)onDrawI,(void**)&originalDrawI},
        {26,(void*)onCreateVB,(void**)&originalCreateVB},{31,(void*)onUpdateTexture,(void**)&originalUpdateTexture},{34,(void*)onStretch,(void**)&originalStretch}};
    size_t created=0;for(auto&h:hooks){if(MH_CreateHook(v[h.slot],h.replacement,h.original)!=MH_OK)break;created++;}
    bool complete=created==sizeof(hooks)/sizeof(hooks[0]);if(complete)for(auto&h:hooks)if(MH_EnableHook(v[h.slot])!=MH_OK){complete=false;break;}
    if(!complete){for(size_t i=0;i<created;i++){MH_DisableHook(v[hooks[i].slot]);MH_RemoveHook(v[hooks[i].slot]);*hooks[i].original=nullptr;}log("D3D9 observation installation failed; using native captions without hover");return;}
    deviceHooked=true;log("D3D9 observation installed; hover uses actual final text-layer quads");
}
HRESULT WINAPI onCreateDevice(IDirect3D9*d,UINT a,D3DDEVTYPE type,HWND hwnd,DWORD flags,D3DPRESENT_PARAMETERS*p,IDirect3DDevice9**out){HRESULT r=originalCreateDevice(d,a,type,hwnd,flags,p,out);if(SUCCEEDED(r)&&out&&*out){gameWindow=hwnd;observeStudyKeys(hwnd);installDevice(*out);}return r;}
void installCreateDevice(IDirect3D9*d){if(originalCreateDevice)return;auto v=*(void***)d;if(MH_CreateHook(v[16],(void*)onCreateDevice,(void**)&originalCreateDevice)==MH_OK)MH_EnableHook(v[16]);}
