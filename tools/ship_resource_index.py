from pathlib import Path
import json
import struct
import yaml
ROOT=Path(__file__).resolve().parents[1]
segments=json.loads((ROOT/"assets/local/segments/segments.json").read_text())["segments"]
indices={item["name"]:item["index"] for item in segments}
records=[]
segment_ids=[0]*64
for file in sorted((ROOT/"third_party/starship/assets/yaml/us/rev1").glob("*.y*ml")):
    if file.stem not in indices: continue
    data=yaml.safe_load(file.read_text(encoding="utf8"))
    mapping=data.get(":config",{}).get("segments",[])
    if mapping: segment_ids[indices[file.stem]]=mapping[0][0]
    for key,item in data.items():
        if not isinstance(item,dict) or "offset" not in item or key.startswith(":"): continue
        count=item.get("count",0)
        if file.stem=="ast_corneria" and item.get("type")=="SF64:TRIANGLE":
            raw=(ROOT/"assets/local/segments"/f"{file.stem}.bin").read_bytes()
            at=item["offset"]&0xffffff
            vertex_count=1+max(struct.unpack_from(">"+"H"*(count*3),raw,at))
            mesh=item.get("mesh_offset",(item["offset"]+count*6+3)&~3)
            mesh_name=item["mesh_symbol"].replace("OFFSET",f"{mesh:07X}")
            assert (mesh&0xffffff)+vertex_count*12<=len(raw), "Invalid Corneria collision mesh"
            records.append((f"__OTR__{file.stem}/{mesh_name}","VEC3F","",indices[file.stem],
                            mesh&0xffffff,0,0,vertex_count,0))
        if item.get("type")=="SF64:MSG_TABLE":
            raw=(ROOT/"assets/local/segments"/f"{file.stem}.bin").read_bytes()
            at=item["offset"]&0xffffff
            while True:
                msg_id,pointer=struct.unpack_from(">iI",raw,at+count*8)
                count+=1
                if msg_id==-1: break
                offset=pointer-item["vram"]
                assert 0<=offset<len(raw) and offset%2==0, "Invalid message ROM pointer"
                records.append((f"__OTR__{file.stem}/gMsg_ID_{msg_id}","SF64:MESSAGE","",
                                indices[file.stem],offset,0,0,0,0))
        records.append((f"__OTR__{file.stem}/{key}",item.get("type",""),item.get("format",""),
                        indices[file.stem],item["offset"]&0xffffff,item.get("width",0),item.get("height",0),count,item.get("vram",0)))
records.sort(key=lambda record: record[0])
assert len({record[0] for record in records})==len(records), "Duplicate resource names"
lines=["// Generated from Starship US Rev A resource metadata; sorted by name; contains no ROM bytes.","static const ShipResource SHIP_RESOURCES[] = {"]
for record in records:
    name,kind,fmt,index,offset,width,height,count,vram=record
    lines.append(f'    {{{json.dumps(name)},{json.dumps(kind)},{json.dumps(fmt)},{index},{offset},{width},{height},{count},{vram}}},')
lines += ["};",""]
lines += ["static const unsigned SHIP_SEGMENT_IDS[64] = {"+",".join(map(str,segment_ids))+"};",""]
output=ROOT/"assets/metadata/ship_resources.inc"
text="\n".join(lines)
if not output.exists() or output.read_text(encoding="utf8")!=text:
    output.write_text(text,encoding="utf8")
print("Starship resource metadata:",len(records))
