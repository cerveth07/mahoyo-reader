using System;
using System.IO;
using System.Collections.Generic;

namespace Mahoyo.Book {
    // Marching squares on antialiased coverage. Coordinates on a shared edge
    // are interpolated in a fixed direction so neighboring cells join exactly.
    static class TypeContours {
        const int threshold=83;
        static int Fraction(int a,int b){return Math.Max(1,Math.Min(15,(int)Math.Round(16.0*(threshold-a)/(b-a))));}
        static int Point(int x,int y){return ((y+32)<<12)|(x+32);}
        static int X(int point){return (point&4095)-32;}
        static int Y(int point){return (point>>12)-32;}
        static readonly int[][] segments={
            new int[0],new[]{0,3},new[]{1,0},new[]{1,3},new[]{2,1},new[]{0,3,2,1},new[]{2,0},new[]{2,3},
            new[]{3,2},new[]{0,2},new[]{1,0,3,2},new[]{1,2},new[]{3,1},new[]{0,1},new[]{3,0},new int[0]
        };
        internal static byte[] Trace(byte[] mask,int w,int h){
            var edges=new Dictionary<int,int>();var endpoints=new int[4];
            for(int y=-1;y<h;y++)for(int x=-1;x<w;x++){
                int tl=y>=0&&x>=0?mask[y*w+x]:0,tr=y>=0&&x+1<w?mask[y*w+x+1]:0;
                int bl=y+1<h&&x>=0?mask[(y+1)*w+x]:0,br=y+1<h&&x+1<w?mask[(y+1)*w+x+1]:0;
                int cell=(tl>=threshold?1:0)|(tr>=threshold?2:0)|(br>=threshold?4:0)|(bl>=threshold?8:0);
                if(cell==0||cell==15)continue;
                int x0=x*16+8,y0=y*16+8;
                if((tl>=threshold)!=(tr>=threshold))endpoints[0]=Point(x0+Fraction(tl,tr),y0);
                if((tr>=threshold)!=(br>=threshold))endpoints[1]=Point(x0+16,y0+Fraction(tr,br));
                if((bl>=threshold)!=(br>=threshold))endpoints[2]=Point(x0+Fraction(bl,br),y0+16);
                if((tl>=threshold)!=(bl>=threshold))endpoints[3]=Point(x0,y0+Fraction(tl,bl));
                var pairs=segments[cell];for(int i=0;i<pairs.Length;i+=2)edges.Add(endpoints[pairs[i]],endpoints[pairs[i+1]]);
            }
            if(edges.Count==0)return new byte[0];
            var keys=new int[edges.Count];edges.Keys.CopyTo(keys,0);
            var ends=new List<int>();var xs=new List<int>();var ys=new List<int>();
            int xMin=32767,xMax=-32768,yMin=32767,yMax=-32768;
            foreach(int start in keys){
                if(!edges.ContainsKey(start))continue;
                var loop=new List<int>();int cur=start;
                do{int next=edges[cur];edges.Remove(cur);loop.Add(cur);cur=next;}while(cur!=start);
                int count=loop.Count;var kept=new List<int>();int last=loop[count-1];
                for(int i=0;i<count;i++){
                    int b=loop[i],c=loop[(i+1)%count],dx=X(c)-X(last),dy=Y(c)-Y(last);
                    long cross=(long)dx*(Y(b)-Y(last))-(long)dy*(X(b)-X(last));
                    int dot=(X(b)-X(last))*(X(c)-X(b))+(Y(b)-Y(last))*(Y(c)-Y(b));
                    if(i>0&&dot>=0&&cross*cross<=4L*(dx*dx+dy*dy))continue;
                    kept.Add(b);last=b;
                }
                if(kept.Count<3)continue;
                foreach(int pt in kept){
                    int px=(int)Math.Round(X(pt)*14.0/16),py=896-(int)Math.Round(Y(pt)*14.0/16);
                    xs.Add(px);ys.Add(py);xMin=Math.Min(xMin,px);xMax=Math.Max(xMax,px);yMin=Math.Min(yMin,py);yMax=Math.Max(yMax,py);
                }
                ends.Add(xs.Count-1);
            }
            if(ends.Count==0)return new byte[0];
            using(var data=new MemoryStream()){
                TypeTables.Word(data,ends.Count);TypeTables.Word(data,xMin);TypeTables.Word(data,yMin);TypeTables.Word(data,xMax);TypeTables.Word(data,yMax);
                foreach(int end in ends)TypeTables.Word(data,end);
                TypeTables.Word(data,0);
                for(int i=0;i<xs.Count;i++)data.WriteByte(1);
                WriteDeltas(data,xs);WriteDeltas(data,ys);return data.ToArray();
            }
        }
        static void WriteDeltas(Stream s,List<int> coords){int last=0;foreach(int coord in coords){TypeTables.Word(s,coord-last);last=coord;}}
    }
}
