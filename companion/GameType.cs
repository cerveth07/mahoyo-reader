using System;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using System.Drawing;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Collections.Generic;
using System.Reflection;

namespace Mahoyo.Book {
    // Build the process-private face used by the game's translated story text.
    static class GameType {
        internal static void Make(string archive,string output){
            byte[] game=File.ReadAllBytes(archive);
            using(var source=Assembly.GetExecutingAssembly().GetManifestResourceStream("Mahoyo.FontRecipe"))
            using(var r=new BinaryReader(new GZipStream(source,CompressionMode.Decompress))){
                if(Encoding.ASCII.GetString(r.ReadBytes(8))!="MHFT0001")throw new InvalidDataException("Font recipe");
                byte[] digest=r.ReadBytes(32);using(var hash=SHA256.Create())if(!hash.ComputeHash(game).SequenceEqual(digest))throw new InvalidDataException("Unsupported game font archive");
                byte[] template=r.ReadBytes(r.ReadInt32());var entries=new Dictionary<string,int>();
                int n=BitConverter.ToInt32(game,12),baseOffset=16+128*n;
                for(int pos=16;pos<baseOffset;pos+=128){
                    int end=Array.IndexOf(game,(byte)0,pos,96);
                    entries.Add(Encoding.ASCII.GetString(game,pos,end-pos),baseOffset+BitConverter.ToInt32(game,pos+96));
                }
                var pages=new byte[r.ReadUInt16()][];
                for(int i=0;i<pages.Length;++i)pages[i]=ReadSheet(game,entries[Encoding.ASCII.GetString(r.ReadBytes(r.ReadByte()))]);
                int count=r.ReadUInt16();var glyphs=new List<byte[]>(count+1);var advances=new List<int>(count+1);
                using(var fallback=new SystemGlyph()){
                    glyphs.Add(TypeContours.Trace(fallback.Draw('□'),73,73));advances.Add(1022);
                    for(int i=0;i<count;i++){
                        char ch=(char)r.ReadUInt16();int sheet=r.ReadByte(),x=r.ReadUInt16(),y=r.ReadUInt16(),w=r.ReadByte(),h=r.ReadByte();
                        byte[] ink;
                        if(sheet==255)ink=fallback.Draw(ch);
                        else{
                            if(x+w>1024||y+h>1024)throw new InvalidDataException("Glyph bounds");
                            ink=new byte[w*h];for(int j=0;j<h;j++)Buffer.BlockCopy(pages[sheet],(y+j)*1024+x,ink,j*w,w);
                        }
                        glyphs.Add(TypeContours.Trace(ink,w,h));advances.Add(w*14);
                    }
                }
                var result=TypeTables.Assemble(template,glyphs,advances);
                string tmp=output+"."+Guid.NewGuid().ToString("N")+".tmp";
                try{File.WriteAllBytes(tmp,result);if(File.Exists(output))File.Replace(tmp,output,null);else File.Move(tmp,output);}
                finally{if(File.Exists(tmp))File.Delete(tmp);}
            }
        }
        // Each supported regular font sheet is one indexed 1024 x 1024 tile.
        // Archive and recipe are paired by SHA-256 before reading tile offsets.
        static byte[] ReadSheet(byte[] game,int start){
            const int side=1024;int descriptor=start+24;
            if(Encoding.ASCII.GetString(game,start,6)!="mrgd00"||BitConverter.ToUInt16(game,descriptor)!=side||
               BitConverter.ToUInt16(game,descriptor+2)!=side)throw new InvalidDataException("Font sheet");
            int tile=descriptor+BitConverter.ToUInt16(game,start+16)*2048+BitConverter.ToUInt16(game,start+18);
            if(Encoding.ASCII.GetString(game,tile,4)!="MZX0"||BitConverter.ToInt32(game,tile+4)!=side*side)throw new InvalidDataException("Font tile");
            var words=new ushort[side*side/2];int input=tile+8,outAt=0,remaining=0,ringAt=0;var ring=new ushort[64];
            while(outAt<words.Length){
                int code=game[input++],mode=code&3,count=mode==2?1:(code>>2)+1;
                if(remaining<=0)remaining=4096;
                if(outAt+count>words.Length)throw new InvalidDataException("Font tile length");
                if(mode==0){
                    ushort previous=remaining==4096?(ushort)0:words[outAt-1];
                    for(int j=0;j<count;j++)words[outAt++]=previous;
                }else if(mode==1){
                    int distance=game[input++]+1;
                    for(int j=0;j<count;j++){words[outAt]=words[outAt-distance];outAt++;}
                }else if(mode==2)words[outAt++]=ring[code>>2];
                else for(int j=0;j<count;j++){ushort v=BitConverter.ToUInt16(game,input);input+=2;words[outAt++]=v;ring[ringAt++&63]=v;}
                remaining-=count;
            }
            var pixels=new byte[side*side];Buffer.BlockCopy(words,0,pixels,0,pixels.Length);
            var alpha=new byte[256];int colors=descriptor+16;
            // Match the zero-coverage entry to the blank atlas corner. Both
            // palette orders occur in HUNEX indexed resources.
            int blank=pixels[0];bool swapped=game[colors+blank*4+3]!=0;
            for(int i=0;i<256;i++){int slot=swapped?(i&231)|((i&8)<<1)|((i&16)>>1):i;
                int a=game[colors+slot*4+3];alpha[i]=(byte)(game[colors+slot*4]*Math.Min(255,2*a+a/64)/255);}
            for(int i=0;i<pixels.Length;i++)pixels[i]=alpha[pixels[i]];return pixels;
        }
        sealed class SystemGlyph:IDisposable {
            readonly Bitmap bitmap=new Bitmap(73,73,PixelFormat.Format32bppRgb);
            readonly Graphics g;readonly Font font=new Font("SimSun",67,FontStyle.Regular,GraphicsUnit.Pixel);
            readonly StringFormat format=new StringFormat(StringFormat.GenericTypographic);
            public SystemGlyph(){g=Graphics.FromImage(bitmap);g.TextRenderingHint=TextRenderingHint.AntiAliasGridFit;format.Alignment=StringAlignment.Center;format.LineAlignment=StringAlignment.Center;}
            public byte[] Draw(char ch){
                g.Clear(Color.Black);g.DrawString(ch.ToString(),font,Brushes.White,new RectangleF(0,0,73,73),format);
                var b=bitmap.LockBits(new Rectangle(0,0,73,73),ImageLockMode.ReadOnly,bitmap.PixelFormat);
                var color=new byte[b.Stride*73];try{Marshal.Copy(b.Scan0,color,0,color.Length);}finally{bitmap.UnlockBits(b);}
                var mask=new byte[73*73];for(int i=0;i<mask.Length;i++)mask[i]=color[(i/73)*(color.Length/73)+4*(i%73)];return mask;
            }
            public void Dispose(){format.Dispose();font.Dispose();g.Dispose();bitmap.Dispose();}
        }
    }
}
