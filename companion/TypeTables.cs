using System;
using System.IO;
using System.Text;
using System.Linq;
using System.Collections.Generic;

namespace Mahoyo.Book {
    // Preserve the cmap, name and OS/2 tables prepared by fontTools. The
    // player's glyphs supply the variable tables, bearings and final checksums.
    static class TypeTables {
        internal static void Word(Stream s,int value){s.WriteByte((byte)(value>>8));s.WriteByte((byte)value);}
        static void Long(Stream s,uint v){Word(s,(int)(v>>16));Word(s,(int)v);}
        static int W(byte[] b,int o){return b[o]*256+b[o+1];}
        static int I(byte[] b,int o){return (W(b,o)<<16)|W(b,o+2);}
        static void SetW(byte[] b,int o,int n){b[o]=(byte)(n>>8);b[o+1]=(byte)n;}
        static void SetI(byte[] b,int o,uint n){SetW(b,o,(int)(n>>16));SetW(b,o+2,(int)n);}
        static uint Sum(byte[] b){
            uint sum=0;for(int i=0;i<b.Length;i++)sum+=((uint)b[i])<<(24-8*(i%4));return sum;
        }
        internal static byte[] Assemble(byte[] template,List<byte[]> glyphs,List<int> advances){
            var tables=new SortedDictionary<string,byte[]>(StringComparer.Ordinal);
            int count=W(template,4);for(int k=0;k<count;k++){
                int p=12+16*k;string tag=Encoding.ASCII.GetString(template,p,4);var b=new byte[I(template,p+12)];
                Buffer.BlockCopy(template,I(template,p+8),b,0,b.Length);tables.Add(tag,b);
            }
            var head=tables["head"];var hhea=tables["hhea"];var maxp=tables["maxp"];
            SetI(head,8,0);SetW(head,50,1);SetW(hhea,34,glyphs.Count);
            int pts=0,contours=0,xMin=0,yMin=0,xMax=0,yMax=0,advance=0,minR=0;
            using(var shape=new MemoryStream())using(var loc=new MemoryStream())using(var metrics=new MemoryStream()){
                for(int i=0;i<glyphs.Count;i++){
                    var g=glyphs[i];Long(loc,(uint)shape.Position);int left=0;
                    if(g.Length>0){
                        int n=W(g,0);left=(short)W(g,2);int right=(short)W(g,6);
                        contours=Math.Max(contours,n);pts=Math.Max(pts,W(g,8+2*n)+1);
                        xMin=Math.Min(xMin,left);xMax=Math.Max(xMax,right);yMin=Math.Min(yMin,(short)W(g,4));yMax=Math.Max(yMax,(short)W(g,8));
                        minR=Math.Min(minR,advances[i]-right);shape.Write(g,0,g.Length);if((shape.Position&1)!=0)shape.WriteByte(0);
                    }
                    advance=Math.Max(advance,advances[i]);Word(metrics,advances[i]);Word(metrics,left);
                }
                Long(loc,(uint)shape.Position);tables["glyf"]=shape.ToArray();tables["loca"]=loc.ToArray();tables["hmtx"]=metrics.ToArray();
            }
            SetW(maxp,6,pts);SetW(maxp,8,contours);SetW(hhea,10,advance);SetW(hhea,12,xMin);SetW(hhea,14,minR);SetW(hhea,16,xMax);
            SetW(head,36,xMin);SetW(head,38,yMin);SetW(head,40,xMax);SetW(head,42,yMax);
            using(var ttf=new MemoryStream()){
                ttf.Write(template,0,12);int offset=12+count*16,headOffset=0;
                foreach(var pair in tables){
                    var tag=Encoding.ASCII.GetBytes(pair.Key);ttf.Write(tag,0,4);Long(ttf,Sum(pair.Value));Long(ttf,(uint)offset);Long(ttf,(uint)pair.Value.Length);
                    if(pair.Key=="head")headOffset=offset;offset+=(pair.Value.Length+3)&~3;
                }
                foreach(var b in tables.Values){ttf.Write(b,0,b.Length);while((ttf.Position&3)!=0)ttf.WriteByte(0);}
                byte[] result=ttf.ToArray();SetI(result,headOffset+8,unchecked(0xb1b0afba-Sum(result)));return result;
            }
        }
    }
}
