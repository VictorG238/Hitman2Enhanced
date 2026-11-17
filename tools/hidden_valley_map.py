#!/usr/bin/env python3
"""Create a local experimental 3D view of Hidden Valley route candidates.

Usage: python -I tools/hidden_valley_map.py ARCHIVE OUTPUT.html
Source assets stay local. Coordinates and transforms are hypotheses; see EXP-006.
"""
import argparse
import hashlib
import json
import math
import re
import struct
import zipfile
import zlib
from pathlib import Path

EXPECTED = '5e3676e996283886ad306dba09267947c198a81af7b3853d0056b1768787a5b1'


def extract(path):
    raw = path.read_bytes()
    if hashlib.sha256(raw).hexdigest() != EXPECTED:
        raise ValueError('Unexpected archive hash; this exploratory layout is build-specific')
    with zipfile.ZipFile(path) as archive:
        def member(ext):
            names = [n for n in archive.namelist() if n.upper().endswith(ext)]
            if len(names) != 1:
                raise ValueError('Expected exactly one ' + ext)
            return archive.read(names[0])
        packed, names, prm = member('.GMS'), member('.BUF'), member('.PRM')
    data = zlib.decompress(packed[9:], -15)
    if len(data) != struct.unpack_from('<I', packed)[0]:
        raise ValueError('Inflated length does not match header')
    table = struct.unpack_from('<I', data)[0]
    count = struct.unpack_from('<I', data, table)[0]
    if count <= 3931 or table + 4 + 8 * count > len(data):
        raise ValueError('Unexpected scene table')

    def item(index):
        entry = struct.unpack_from('<I', data, table + 4 + 8 * index)[0]
        offset = (entry & 0xffffff) * 4
        name, matrix, position, model = struct.unpack_from('<4I', data, offset)
        end = names.index(b'\0', name)
        label = names[name:end].decode('latin1')
        pos = struct.unpack_from('<3f', data, position)
        rot = struct.unpack_from('<9f', data, matrix)
        if not all(math.isfinite(x) and abs(x) < 1e6 for x in pos + rot):
            raise ValueError('Invalid transformation')
        return label, pos, rot, model

    patrol = []
    for i in range(3410, 3625):
        name, pos, _, _ = item(i)
        if name.startswith('Arrow'):
            patrol.append({'id': i, 'name': name, 'p': pos})

    checkpoint = []
    for i in (3524, 3525, 3526, 3528, 3529, 3530, 3540, 3542, 3543):
        name, pos, _, _ = item(i)
        if not (name.startswith('Examine') or name == 'End'):
            raise ValueError('Unexpected checkpoint point at index ' + str(i))
        checkpoint.append({'id': i, 'name': name, 'p': pos})

    guards = []
    for i in (3531, 3533, 3535, 3537):
        name, pos, _, _ = item(i)
        if not name.startswith('SoldierNinjaInside'):
            raise ValueError('Unexpected checkpoint guard at index ' + str(i))
        guards.append({'id': i, 'name': name, 'p': pos})

    splines = []
    for i in range(3922, 3932):
        name, pos, matrix, anchor = item(i)
        if not name.startswith('TruckSpline_'):
            raise ValueError('Unexpected spline name at table index ' + str(i))
        n = struct.unpack_from('<H', prm, anchor + 0x16)[0]
        back, vertex = struct.unpack_from('<II', prm, anchor + 0x18)
        if back != anchor - 0x3c or not 1 <= n <= 4096 or vertex + 36*n > len(prm):
            raise ValueError('Invalid PRM vertex block for ' + name)
        vertices = [struct.unpack_from('<3f', prm, vertex + 36*j) for j in range(n)]
        if not all(math.isfinite(v) and abs(v) < 1e6 for pt in vertices for v in pt):
            raise ValueError('Invalid vertex')
        splines.append({'name': name, 'p': pos, 'm': matrix, 'v': vertices})
    def script_records(script):
        records = []
        for found in re.finditer(re.escape(script.encode('ascii')), data, re.I):
            start = found.end() + 1
            if data[start] != 0x84:
                raise ValueError('Unexpected script declaration tag')
            end = data.index(b'\0', start + 1)
            fields = data[start + 1:end].decode('ascii').split(';')
            cursor, values = end + 1, {}
            for field in filter(None, fields):
                tag, value = data[cursor], struct.unpack_from('<I', data, cursor + 1)[0]
                if tag not in (2, 3, 8, 11):
                    raise ValueError('Unexpected script property tag')
                key = field.split()[-1].split('=')[0]
                values[key] = value
                cursor += 5
            records.append(values)
        return records

    def referenced_name(value):
        if not value & 0x80000000 or not (value & 0x7fffffff):
            return None
        index = (value & 0x7fffffff) - 1
        if index >= count:
            raise ValueError('Reference outside scene table')
        return item(index)[0]

    vans = script_records('Van32a.sdl')
    ninjas = script_records('CheckPointNinja.sdl')
    expected_paths = ['TruckSpline_%02d' % n for n in range(1, 11)]
    if len(vans) != 5 or len(ninjas) != 4:
        raise ValueError('Unexpected number of checkpoint scripts')
    for van in vans:
        if [referenced_name(van['rPath%d' % n]) for n in range(1, 11)] != expected_paths:
            raise ValueError('Truck route references do not match expected labels')
    for guard, ninja in zip(guards, ninjas):
        guard['script_id'] = ninja['id']
        guard['references'] = {key: referenced_name(ninja[key]) for key in ('examine', 'examine2', 'end')}
    return {'patrol': patrol, 'checkpoint': checkpoint, 'guards': guards,
            'splines': splines, 'start_paths': [x['startPath'] for x in vans],
            'sha256': EXPECTED}


PAGE = r'''<!doctype html><html lang="es"><meta charset="utf-8"><title>Hidden Valley: candidatos de rutas</title>
<style>body{margin:0;background:#101820;color:#e9f0f4;font:15px system-ui}header{padding:14px 18px;max-width:1000px}h1{font-size:21px;margin:0 0 6px}p{margin:6px 0;line-height:1.4}button{background:#253748;border:1px solid #8199a9;color:white;padding:8px;border-radius:5px;cursor:pointer}canvas{display:block;width:100%;height:70vh;background:#0c121a;touch-action:none}.orange{color:#ffbb6b}.cyan{color:#68d8ee}</style>
<header><h1>Hidden Valley: geometría candidata de rutas</h1><p><b>Exploratorio:</b> puntos del nivel y vértices de objetos llamados TruckSpline. La transformación correcta, las trayectorias reales, las colisiones y los tiempos siguen sin verificar; la cercanía no demuestra un atropello.</p><p>Las cinco configuraciones de Van32a referencian las diez spline; cuatro ninjas del control referencian puntos Examine/End. Los valores startPath son 8, 6, 4, 2 y 0; el significado de esos valores y las paradas aún no se ha validado.</p><p><span class="orange">Naranja: puntos Arrow</span> · <span class="cyan">Celeste: vértices TruckSpline</span> · <span style="color:#f4ef6a">Amarillo: puntos Examine/End del control</span> · <span style="color:#ed86be">Rosa: posiciones de ninjas del control</span>. Arrastra para girar y usa la rueda para acercar.</p><button id="mode"></button> <button id="focus">Centrar punto de control</button> <button id="reset">Ver todo</button><p id="desc"></p></header><canvas id="view"></canvas>
<script>const DATA=__DATA__;let mode=0,az=.3,el=.8,zoom=1.6,drag=null,center=[-48000,-2800,21000];const c=document.getElementById('view'),ctx=c.getContext('2d'),btn=document.getElementById('mode'),desc=document.getElementById('desc');
function build(){let pts=[]; for(const x of DATA.patrol)pts.push({p:x.p,c:'#ffbb6b',r:3});for(const x of DATA.checkpoint)pts.push({p:x.p,c:'#f4ef6a',r:5});for(const x of DATA.guards)pts.push({p:x.p,c:'#ed86be',r:4});for(const s of DATA.splines)for(const v of s.v){let p=mode? [0,1,2].map(k=>s.p[k]+s.m[3*k]*v[0]+s.m[3*k+1]*v[1]+s.m[3*k+2]*v[2]):v.map((x,k)=>x+s.p[k]);pts.push({p,c:'#68d8ee',r:1.6})}return pts}
function draw(){btn.textContent=mode?'Transformación: matriz del archivo':'Transformación: traslación';desc.textContent=mode?'Hipótesis B: matriz 3×3 y posición leídas del objeto.':'Hipótesis A: posición del objeto sumada a cada vértice. Pulsa para comparar con la matriz.';let w=c.clientWidth,h=c.clientHeight,dpr=devicePixelRatio||1;c.width=w*dpr;c.height=h*dpr;ctx.setTransform(dpr,0,0,dpr,0,0);ctx.clearRect(0,0,w,h);let pts=build(),scale=Math.min(w,h)/85000*zoom,ca=Math.cos(az),sa=Math.sin(az),ce=Math.cos(el),se=Math.sin(el);let a=pts.map(o=>{let x=o.p[0]-center[0],y=o.p[2]-center[2],z=o.p[1]-center[1],u=x*ca-y*sa,v=x*sa+y*ca;return {...o,x:w/2+u*scale,y:h/2-(z*ce-v*se)*scale,depth:z*se+v*ce}}).sort((a,b)=>a.depth-b.depth);for(let o of a){ctx.fillStyle=o.c;ctx.globalAlpha=.8;ctx.beginPath();ctx.arc(o.x,o.y,o.r,0,7);ctx.fill()}ctx.globalAlpha=1;ctx.fillStyle='#9aabb8';ctx.fillText('Arrow: '+DATA.patrol.length+' · Control: '+DATA.checkpoint.length+' · Ninjas: '+DATA.guards.length+' · Vértices: '+(pts.length-DATA.patrol.length-DATA.checkpoint.length-DATA.guards.length),14,h-14)}
btn.onclick=()=>{mode=1-mode;draw()};document.getElementById('focus').onclick=()=>{center=[-46200,-3200,21300];zoom=7;draw()};document.getElementById('reset').onclick=()=>{center=[-48000,-2800,21000];zoom=1.6;draw()};c.onpointerdown=e=>{drag=[e.clientX,e.clientY];c.setPointerCapture(e.pointerId)};c.onpointermove=e=>{if(!drag)return;az+=(e.clientX-drag[0])*.006;el=Math.max(-1.5,Math.min(1.5,el+(e.clientY-drag[1])*.006));drag=[e.clientX,e.clientY];draw()};c.onpointerup=()=>drag=null;c.onwheel=e=>{e.preventDefault();zoom=Math.max(.3,Math.min(30,zoom*Math.exp(-e.deltaY*.001)));draw()};window.onresize=draw;draw();</script></html>'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    result = extract(args.archive)
    encoded = json.dumps(result, ensure_ascii=True, separators=(',', ':')).replace('<', '\\u003c')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(PAGE.replace('__DATA__', encoded), encoding='utf-8')
    print('Wrote', args.output, 'patrol points', len(result['patrol']),
          'spline vertices', sum(len(s['v']) for s in result['splines']))


if __name__ == '__main__':
    main()
