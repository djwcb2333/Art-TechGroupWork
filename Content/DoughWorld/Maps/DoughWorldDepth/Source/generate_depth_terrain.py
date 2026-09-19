"""Authored metric terrain. Original documents are read only; no Unreal side effects."""
import json, math, hashlib
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFont

P = Path(__file__).parent
SOURCE = P.parent/'Handoff'/'Originals'/'10_document.json'
D = json.loads(SOURCE.read_text(encoding='utf-8-sig'))
N, SIZE = 505, 500.0
SPACING = SIZE/(N-1)
Y, X = np.mgrid[0:N,0:N].astype(float)*SPACING

def smooth(t):
    t = np.clip(t, 0, 1)
    return t*t*(3-2*t)

def distance_segment(a,b,xx=X,yy=Y):
    vx,vy = b[0]-a[0],b[1]-a[1]
    t = np.clip(((xx-a[0])*vx+(yy-a[1])*vy)/(vx*vx+vy*vy),0,1)
    return np.hypot(xx-a[0]-t*vx,yy-a[1]-t*vy),t

def sdf_polygon(points):
    inside = np.zeros(X.shape, dtype=bool)
    dist = np.ones_like(X)*1e6
    for a,b in zip(points,points[1:]+points[:1]):
        dist=np.minimum(dist,distance_segment(a,b)[0])
        crossing=((a[1]>Y)!=(b[1]>Y)) & (X<(b[0]-a[0])*(Y-a[1])/(b[1]-a[1]+1e-15)+a[0])
        inside ^= crossing
    return np.where(inside,-dist,dist)

POLYGONS = {
 'qianceng': [[146,211],[155,178],[184,152],[220,148],[254,156],[289,148],[322,170],[343,206],[350,254],[342,300],[326,331],[303,361],[283,389],[248,393],[209,381],[174,363],[145,341],[132,311],[130,273],[143.3,233.3]],
 'jidi': [[193,232],[212,221],[246,224],[273,229],[286,247],[281,269],[263,286],[232,289],[207,283],[189,269],[182,249]],
 'fajiao': [[96.7,132],[116,132],[140,145],[151,161],[143,179],[133,195],[135,215],[143.3,233.3],[134,247],[109,260],[80,249],[57,229],[44,201],[49,180],[67,161],[84,143]],
 'jiaomu': [[96.7,132],[77,139],[48,123],[23,110],[9,87],[13,63],[21,39],[39,27],[70,29],[100,41],[119,61],[123,87],[113,112]],
 'meijun': [[331,133],[352,125],[377,133],[409,128],[432,143],[448,164],[453,189],[442,218],[421,242],[390,252],[357,246],[330,234],[312,215],[310,182],[319,153]],
}
SDF={k:sdf_polygon(v) for k,v in POLYGONS.items()}
W={k:1-smooth((d+4)/28) for k,d in SDF.items()}

# Bread-like ground is formed from deliberately oriented, low-frequency folds.
# Broad asymmetric region outlines replace the old ellipse/gray-value platforms.
H = 1.15 + .32*np.sin((X+.34*Y)/57) + .24*np.cos((Y-.22*X)/64)
fold = .28*np.sin((.62*X+Y)/29)+.16*np.sin((X-.8*Y)/39)
H += fold * (1-W['jidi'])
def blend(target,w):
    global H
    H=H*(1-w)+target*w

blend(6.8+.45*np.sin((X+Y)/44)+.30*np.cos(Y/21), W['fajiao'])
blend(8.8+.50*np.sin(X/40)+.24*np.cos((X+Y)/30), W['jiaomu'])
blend(-4.4+.38*np.sin((X-2*Y)/53)+.32*np.cos((X+Y)/42), W['meijun'])
baseweight=1-smooth(np.maximum(SDF['jidi'],0)/38)
baseheight=6.4+.001*(X-233)+.0015*(Y-255)
blend(baseheight,baseweight)

# The NE corner is solid inaccessible background, not the deep-world arena.
background_line=[[318,37],[357,56],[395,75],[424,102],[471,113],[500,99]]
bd=np.minimum.reduce([distance_segment(a,b)[0] for a,b in zip(background_line,background_line[1:])])
ridge=11.5*np.exp(-(bd/25)**2)*(1-smooth((Y-112)/27))
H+=ridge
# Distant southern and western escarpments have nonuniform outlines and wide foreground.
H+=4.5*np.exp(-((Y-(438+10*np.sin(X/62)))/35)**2)
H+=3.0*np.exp(-((X-(22+7*np.sin(Y/53)))/16)**2)*(1-smooth((Y-290)/95))

def apply_path(points, heights, inner=4.0, outer=12.0):
    global H
    nearest=np.full_like(H,1e9); target=H.copy()
    for a,b,za,zb in zip(points,points[1:],heights,heights[1:]):
        dist,t=distance_segment(a,b)
        choose=dist<nearest
        # Linear longitudinal ramps keep a bounded slope; roundoff is sub-centimetric.
        target=np.where(choose,za+(zb-za)*t,target)
        nearest=np.minimum(nearest,dist)
    blend(target,1-smooth((nearest-inner)/(outer-inner)))

# Ordered tutorial is the original route, without invented repositioned nodes.
DEMO=[p['pos'] for p in D['demo_route']]
DEMO_Z=[1.15,1.65,2.1,5.80,6.34,6.365,6.404,6.4195,6.476]
apply_path(DEMO,DEMO_Z,inner=4.5,outer=14)
# Original facilities retain the complete footprint plus accessible entrance clearances.
FACILITIES=[p for p in D['buildings'] if p['region']=='jidi']
for p in FACILITIES:
    d=np.hypot(X-p['pos'][0],Y-p['pos'][1])
    blend(baseheight,1-smooth((d-10)/13))

# Secondary walking corridors are additive environmental alignment, not new gameplay routes.
ACCESS = [
 {'id':'base_to_wall','points':[[216.7,253.3],[196.7,256.7],[177,249],[159,237],[143.3,233.3]],'z':[6.34,6.365,4.2,1.45,.15]},
 {'id':'wall_to_ferment','points':[[143.3,233.3],[127,235],[107,226],[94,201],[96,179],[110,166.7],[102,149],[96.7,132]],'z':[.15,1.25,3.65,6.0,6.7,6.85,7.4,8.0]},
 {'id':'sentry_to_yeast','points':[[96.7,132],[93,118],[100,106.7],[83.3,93.3],[60,66.7]],'z':[8.0,8.35,8.6,9.0,9.1]},
 {'id':'base_to_mold','points':[[270,233.3],[292,239],[315,238],[337,231],[360,220]],'z':[6.4,4.45,1.4,-2.0,-4.25]},
 {'id':'mold_alternate','points':[[360,220],[373,233],[400,232],[428,216],[434,192],[431,167],[421,148],[418.2,146]],'z':[-4.25,-4.3,-4.45,-4.6,-4.7,-4.8,-4.75,-4.7],'end_note':'Approach ends outside abyss rim; original hole center remains (410,140), reached by explicit interaction, not a walkable path.'},
 {'id':'mold_ruins','points':[[337,231],[324,205],[328,183],[333.3,166.7],[350,156.7]],'z':[-2,-3.3,-4.0,-4.3,-4.5]},
]
for a in ACCESS: apply_path(a['points'],a['z'],inner=3.2,outer=11)

# Terrace waterfall is a sequence of near-vertical drops and level shelves.
crest=np.array([123.3,160.0]); pool=np.array([143.3,201.7]); gate=np.array([143.3,233.3])
flow=pool-crest; fall_length=float(np.linalg.norm(flow)); flow/=fall_length
fall_t=((X-crest[0])*flow[0]+(Y-crest[1])*flow[1])/fall_length
fall_side=np.abs((X-crest[0])*(-flow[1])+(Y-crest[1])*flow[0])
fall_profile=15.75-5.0*smooth((fall_t-.21)/.045)-5.45*smooth((fall_t-.52)/.045)-4.9*smooth((fall_t-.79)/.045)
fall_profile-=.08*np.clip(fall_t,0,1)
fall_w=(1-smooth((fall_side-7)/7))*smooth((fall_t+.2)/.12)*(1-smooth((fall_t-.93)/.15))
blend(fall_profile,fall_w)
crest_dist=np.hypot(X-crest[0],Y-crest[1])
crest_w=1-smooth((crest_dist-5)/12)
# Only upstream crest shelf; downstream drop profile is retained.
crest_w*=1-smooth((fall_t-.08)/.08)
blend(15.75+.006*(160-Y),crest_w)
pool_dist=np.hypot(X-pool[0],Y-pool[1])
blend(.40,1-smooth((pool_dist-6)/8))
stream_dist,stream_t=distance_segment(pool,gate)
stream_bed=.40-.25*stream_t
blend(stream_bed,1-smooth((stream_dist-2.5)/6))
# Lateral terrestrial route crosses the stream gate at the authored point.
gd=np.hypot(X-gate[0],Y-gate[1])
blend(.15,1-smooth((gd-3.0)/5))

# Cut channels along the two boundaries; exact gaps remain at their original XY.
# Their collision and vertical overhang walls must be implemented with separate geometry.
BOUNDARIES=[]
for key,gaps in [('fajiao',[{'id':'mech_wall_1','xy':[143.3,233.3],'width_m':5.5},{'id':'mech_gap_sentry','xy':[96.7,132],'width_m':3.0}]),('jiaomu',[{'id':'mech_gap_sentry','xy':[96.7,132],'width_m':3.0}])]:
    points=POLYGONS[key]
    # Exterior trench preserves region interior area; gap shoulders have visual thickness.
    sd=SDF[key]
    cut=3.8*np.exp(-((sd-2.7)/2.1)**4)
    gapmask=np.ones_like(H)
    for g in gaps:
        gapmask*=smooth((np.hypot(X-g['xy'][0],Y-g['xy'][1])-g['width_m']/2-3)/5)
        i=points.index(g['xy']);a=np.array(points[i-1]);b=np.array(points[(i+1)%len(points)])
        tangent=b-a; tangent=tangent/np.linalg.norm(tangent)
        normal=np.array([-tangent[1],tangent[0]])
        centroid=np.mean(points,axis=0)
        if np.dot(normal,centroid-g['xy'])<0: normal=-normal
        g['inward_normal_xy']=normal.round(6).tolist()
        g['tangent_xy']=tangent.round(6).tolist()
        g['unreal_inward_yaw_degrees']=round(math.degrees(math.atan2(normal[1],normal[0])),4)
        g['unreal_xy_cm']=[(q-250)*100 for q in g['xy']]
    H-=cut*gapmask
    BOUNDARIES.append({'region':key,'closed':True,'polyline_m':points+[points[0]],'gaps':gaps,'external_trench_offset_m':2.7,'trench_depth_m':3.8,'minimum_collision_wall_above_walkable_m':5.0,'gap_collision_policy':'side walls close to physical gate leaves; no hidden opening beside portal','status':'terrain_cut_generated; vertical_mesh_and_collision_required'})

# Exact source holes are separate geometry apertures; heightmap depressions receive their linings.
HOLES=[]
def sample(arr,x,y):
    u=np.clip(x/SPACING,0,N-1); v=np.clip(y/SPACING,0,N-1)
    i,j=int(u),int(v); k,l=min(i+1,N-1),min(j+1,N-1)
    return float(arr[j,i]*(1-u+i)*(1-v+j)+arr[j,k]*(u-i)*(1-v+j)+arr[l,i]*(1-u+i)*(v-j)+arr[l,k]*(u-i)*(v-j))
for p in D['holes']:
    xy=p['pos']; rim=sample(H,*xy)
    radius,depth={'hole_small_1':(2.7,3.2),'hole_vent_1':(3.3,4.0),'hole_collapse_1':(5.5,6.0),'hole_abyss':(8,10)}[p['id']]
    dist=np.hypot(X-xy[0],Y-xy[1]); H-=depth*(1-smooth((dist-radius*.65)/(radius*.35)))
    HOLES.append({'source_id':p['id'],'xy_m':xy,'rim_z_m':rim,'radius_m':radius,'depression_depth_m':depth,'inner_floor_z_m':rim-depth,'note':'Requires independently modelled rim, inner wall and aperture/portal semantics; heightmap cannot form cave ceiling.'})

# Restore safe standing pads at original points outside special landmark holes.
for group in ['monsters','resources','flora','buildings']:
    for p in D[group]:
        if p.get('region')=='shendu': continue
        if p in FACILITIES: continue
        x,y=p['pos']; z=sample(H,x,y)
        rad=7 if group=='buildings' else (3 if group in ('monsters','resources') else 1.2)
        dist=np.hypot(X-x,Y-y)
        blend(z,1-smooth((dist-rad)/5))

# Final constraint projection: decorative pads cannot introduce steps on the primary
# tutorial, and facilities take precedence over overlapping approach corridors.
for a in ACCESS:
    apply_path(a['points'],a['z'],inner=3.2,outer=9)
apply_path(DEMO,DEMO_Z,inner=4.5,outer=14)
for p in FACILITIES:
    d=np.hypot(X-p['pos'][0],Y-p['pos'][1])
    blend(baseheight,1-smooth((d-11)/16))

# Preserve the watercourse over boundary trench cuts: cliffs keep their physical
# closure from the separately authored faces. Water must never climb a terrain lip.
blend(fall_profile,fall_w)
blend(.40,1-smooth((pool_dist-6)/8))
blend(stream_bed,1-smooth((stream_dist-2.5)/6))
blend(.15,1-smooth((gd-3.0)/5))

# The primary land approaches own their center strips where two corridor blends
# meet; this prevents a neighbouring corridor from putting a lip across the path.
for key in ['base_to_wall','wall_to_ferment','base_to_mold']:
    a=next(a for a in ACCESS if a['id']==key)
    apply_path(a['points'],a['z'],inner=3.2,outer=9)
# Facility footprints remain the final local constraint; route lengths carry
# the compensating rise outside the 14 m entrance/building clearance squares.
for p in FACILITIES:
    d=np.hypot(X-p['pos'][0],Y-p['pos'][1])
    blend(baseheight,1-smooth((d-11)/16))

# Solve the western approach as one continuous arclength ramp. The first 11 m
# from the storage anchor stay level, then the full remaining length carries a
# cubic descent. Piecewise per-point ramps would otherwise bunch their rise at
# overlapping facility pads.
western=next(a for a in ACCESS if a['id']=='base_to_wall')
nearest=np.full_like(H,1e9); arc=np.zeros_like(H);total=0
for a,b in zip(western['points'],western['points'][1:]):
    dist,t=distance_segment(a,b);length=math.dist(a,b)
    arc=np.where(dist<nearest,total+t*length,arc);nearest=np.minimum(nearest,dist);total+=length
first_length=math.dist(western['points'][0],western['points'][1])
start=first_length+11
western_height=6.36625-(6.36625-.15)*smooth((arc-start)/(total-start))
blend(western_height,1-smooth((nearest-3.5)/6))

GY,GX=np.gradient(H,SPACING)
SLOPE=np.degrees(np.arctan(np.hypot(GX,GY)))
RAW=np.clip(np.rint(32768+H*128),0,65535).astype('<u2')
RAW.tofile(P/'DoughWorldDepth_505.r16')
Image.fromarray(RAW).save(P/'DoughWorldDepth_Height16.png')
RG=np.empty((N,N,4),np.uint8);RG[:,:,0]=(RAW>>8).astype(np.uint8);RG[:,:,1]=(RAW&255).astype(np.uint8);RG[:,:,2]=0;RG[:,:,3]=255
Image.fromarray(RG).save(P/'HeightRG.png')
np.save(P/'height_m.npy',H.astype(np.float32))
np.save(P/'slope_degrees.npy',SLOPE.astype(np.float32))

# Subtle region material variation; no visibly flat coloured discs.
C=np.zeros((N,N,3))+[215,184,132]
palette={'jidi':[228,204,159],'fajiao':[224,175,83],'jiaomu':[211,178,97],'meijun':[153,134,118]}
for key,c in palette.items():
    w=W[key] if key!='jidi' else baseweight*.7
    C=C*(1-w[:,:,None])+np.array(c)*w[:,:,None]
tex=(.55*np.sin(X/7)*np.sin(Y/11)+.4*np.sin((X+Y)/17))[:,:,None]
C=np.clip(C+tex*3,0,255).astype(np.uint8)
Image.fromarray(C).save(P/'T_DepthRegionColor.png')

def path_report(points,orders=None):
    rows=[]
    for idx,(a,b) in enumerate(zip(points,points[1:])):
        length=math.dist(a,b); steps=max(3,int(math.ceil(length/.25))+1)
        t=np.linspace(0,1,steps); zz=np.array([sample(H,a[0]+v*(b[0]-a[0]),a[1]+v*(b[1]-a[1])) for v in t])
        slopes=np.degrees(np.arctan(np.abs(np.diff(zz))/(length/(steps-1))))
        surface=np.array([sample(SLOPE,a[0]+v*(b[0]-a[0]),a[1]+v*(b[1]-a[1])) for v in t])
        rows.append({'from_order':orders[idx] if orders else idx,'to_order':orders[idx+1] if orders else idx+1,'length_m':round(length,3),'start_z_m':round(float(zz[0]),4),'end_z_m':round(float(zz[-1]),4),'max_longitudinal_slope_deg':round(float(slopes.max()),4),'max_surface_slope_deg':round(float(surface.max()),4),'daily_12deg_pass':bool(slopes.max()<=12 and surface.max()<=12),'exploration_18deg_pass':bool(slopes.max()<=18 and surface.max()<=18)})
    return rows

route_report=path_report(DEMO,list(range(1,10)))
facility_report=[]
for p in FACILITIES:
    x,y=p['pos'];mask=(np.abs(X-x)<=7)&(np.abs(Y-y)<=7)
    facility_report.append({'source_id':p['id'],'xy_m':p['pos'],'tested_square_m':14,'z_m':sample(H,x,y),'max_slope_deg':float(SLOPE[mask].max()),'build_2deg_pass':bool(SLOPE[mask].max()<=2)})

anchors=[]
for group in ['regions','buildings','monsters','resources','flora','holes','mechanisms','art_reference_pins','demo_route']:
    for p in D[group]:
        field='center' if group=='regions' else 'pos'; xy=p[field]
        underground=(p.get('region')=='shendu' or (group=='regions' and p['id']=='shendu')) and group!='art_reference_pins'
        z=-60.0 if underground else sample(H,*xy)
        hole=next((q for q in HOLES if q['source_id']==p.get('id')),None)
        if hole:z=hole['rim_z_m']
        if p.get('id')=='mech_hole_shortcut':z=next(q['rim_z_m'] for q in HOLES if q['source_id']=='hole_small_1')
        anchors.append({'source_ref':f"{group}/{p.get('id',p.get('order'))}",'source_xy_m':xy,'unreal_cm':[(xy[0]-250)*100,(xy[1]-250)*100,z*100],'z_m':round(z,5),'surface_z_m':round(sample(H,*xy),5),'vertical_frame':'underground_at_world_minus_60m' if underground else 'surface_shallow_zero','spawn_object':group not in ('regions','art_reference_pins')})
landmark_values={}
for p in D['landmarks']:
    for field in ['crest_pos','pool_pos','stream_to']:
        xy=p[field];landmark_values[field]={'xy_m':xy,'ground_z_m':sample(H,*xy),'water_z_m':{'crest_pos':15.85,'pool_pos':.65,'stream_to':.20}[field]}

violations=[]
for r in route_report:
    if not r['daily_12deg_pass']:violations.append({'constraint':'daily_route_12deg','detail':r})
for r in facility_report:
    if not r['build_2deg_pass']:violations.append({'constraint':'facility_clearance_2deg','detail':r})
anchor_checks=[]
for xy in [D['demo_route'][0]['pos'],D['landmarks'][0]['crest_pos'],D['holes'][-1]['pos']]:
    ue=[(xy[0]-250)*100,(xy[1]-250)*100]
    recovered=[ue[0]/100+250,ue[1]/100+250]
    anchor_checks.append({'source_xy_m':xy,'unreal_xy_cm':ue,'roundtrip_xy_m':recovered,'max_error_m':max(abs(a-b) for a,b in zip(xy,recovered))})

terrain={'resolution':N,'n':N,'size_m':SIZE,'vertex_spacing_m':SPACING,'origin_unreal_cm':[-25000,-25000,0],'landscape_scale_xyz':[SPACING*100,SPACING*100,100],'raw_encoding':'unsigned16 little endian; 32768+metres*128; R=highbyte,G=lowbyte,B=0,A=255','row_orientation':'row0 is source y=0 / Unreal Y=-25000; no flip or transpose','heights_m':np.round(H,5).tolist(),'original_source_sha256':hashlib.sha256(SOURCE.read_bytes()).hexdigest(),'waterfall':landmark_values,'regions':POLYGONS,'routes':[DEMO]+[a['points'] for a in ACCESS]}
(P/'terrain_data.json').write_text(json.dumps(terrain,ensure_ascii=False,separators=(',',':')),encoding='utf-8')
supplement={'schema':'doughworld_additive_depth_1.0','original_xy_mutations':0,'original_demo_order_mutations':0,'coordinate_transform':{'unreal_x_cm':'(original_x_m-250)*100','unreal_y_cm':'(original_y_m-250)*100','unreal_z_cm':'added_z_m*100'},'anchors':anchors,'boundaries':BOUNDARIES,'holes':HOLES,'waterfall':{'source_ref':'landmarks/butter_waterfall','flow_direction_xy':flow.tolist(),'shelves':[{'flow_t':0,'z_m':15.75},{'flow_t':.255,'z_m':10.73},{'flow_t':.565,'z_m':5.255},{'flow_t':.835,'z_m':.333}],'fall_face_start_t':[.21,.52,.79],'fall_face_end_t':[.255,.565,.835],'shelf_half_width_m':7,'water_surface':landmark_values,'requires_geometric_faces':True},'underground':{'base_z_m':-60,'original_xy_preserved':True,'surface_shendu_policy':'sealed positive background ridge, no open reachable surface boss basin','arena_bounds_reference':{'center_m':[436.7,83.3],'activity_envelope_m':[80,60]},'local_floor_z_m':0,'peripheral_groove_z_m':[-4,-2],'background_wall_height_m':[8,16],'terrain_heightmap_not_used_as_underground_ceiling':True},'paths_additive':ACCESS,'visibility_notes':['Actual fixed camera must be tested in Unreal; this offline heightmap cannot prove visibility.','Boundary meshes and collisions must complete the two closed rings except exact gate openings.','Deep-world original art reference pins are references, never entity placements.']}
(P/'depth_supplement.json').write_text(json.dumps(supplement,ensure_ascii=False,indent=2),encoding='utf-8')
report={'resolution':N,'height_min_m':float(H.min()),'height_max_m':float(H.max()),'raw_min':int(RAW.min()),'raw_max':int(RAW.max()),'quantisation_error_max_m':float(np.max(np.abs((RAW.astype(float)-32768)/128-H))),'tutorial_route':route_report,'facility_clearances':facility_report,'three_noncollinear_anchors':anchor_checks,'source_xy_mutations':0,'demo_order_mutations':0,'unmet_constraints':violations,'secondary_path_reports':{p['id']:path_report(p['points']) for p in ACCESS},'offline_only':True}
(P/'terrain_validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')

# Numerical map graphics, generated directly from data (not edits to reference art).
def font(size):
    for path in [Path('C:/Windows/Fonts/msyh.ttc'),Path('C:/Windows/Fonts/arial.ttf')]:
        if path.exists():return ImageFont.truetype(str(path),size)
    return ImageFont.load_default()
def ramp(values,lo,hi,colors):
    t=np.clip((values-lo)/(hi-lo),0,1)*(len(colors)-1);i=np.floor(t).astype(int);j=np.minimum(i+1,len(colors)-1);f=t-i
    return (np.asarray(colors)[i]*(1-f[:,:,None])+np.asarray(colors)[j]*f[:,:,None]).astype(np.uint8)
height_rgb=ramp(H,-10,18,[[33,53,75],[80,107,119],[175,174,140],[229,207,154],[173,128,67],[244,233,207]])
surface=Image.fromarray(height_rgb).resize((1010,1010),Image.Resampling.BILINEAR)
draw=ImageDraw.Draw(surface)
for pts in POLYGONS.values():draw.line([(x*2.02,y*2.02) for x,y in pts+pts[:1]],fill=(58,49,40),width=2)
draw.line([(x*2.02,y*2.02) for x,y in DEMO],fill=(15,100,228),width=5)
for i,(x,y) in enumerate(DEMO,1):
    u,v=x*2.02,y*2.02;draw.ellipse((u-11,v-11,u+11,v+11),fill='white',outline=(10,70,200),width=2);draw.text((u-5,v-9),str(i),font=font(15),fill=(10,50,160))
for k,xy in [('浅层',[247,334]),('基地',[245,260]),('活跃发酵',[92,203]),('酵母生态',[58,70]),('霉菌低地',[379,179]),('封闭背景 / 地下位于其下方',[426,64])]:draw.text((xy[0]*2.02-35,xy[1]*2.02),k,font=font(18),fill=(18,29,40))
surface.save(P/'Height_Routes_Overview.png')
slope_rgb=ramp(SLOPE,0,45,[[218,239,199],[146,209,132],[255,224,139],[240,156,91],[188,51,51]])
sl=Image.fromarray(slope_rgb).resize((1010,1010),Image.Resampling.NEAREST);sd=ImageDraw.Draw(sl)
sd.line([(x*2.02,y*2.02) for x,y in DEMO],fill=(14,49,167),width=4)
sd.text((24,20),'坡度图：绿 0–12° / 黄 12–18° / 橙红 陡壁与孔边',font=font(25),fill=(20,30,40));sl.save(P/'Slope_Routes_Overview.png')
# Ground profiles with separate water-height markers.
canvas=Image.new('RGB',(1500,950),(249,247,242));dr=ImageDraw.Draw(canvas)
def profile_plot(origin,size,points,title,zmin,zmax,marker_labels=None):
    ox,oy=origin;ww,hh=size;samples=[];distances=[];nodes=[0];length=0
    for a,b in zip(points,points[1:]):
        seg=math.dist(a,b)
        for t in np.linspace(0,1,max(3,int(seg*4))):
            samples.append(sample(H,a[0]+t*(b[0]-a[0]),a[1]+t*(b[1]-a[1])));distances.append(length+t*seg)
        length+=seg;nodes.append(length)
    dr.text((ox,oy-42),title,font=font(24),fill=(30,37,47))
    for z in np.linspace(zmin,zmax,6):
        py=oy+hh-(z-zmin)/(zmax-zmin)*hh
        dr.line((ox,py,ox+ww,py),fill=(218,218,210));dr.text((ox-49,py-9),f'{z:.1f}',font=font(16),fill=(70,76,78))
    curve=[(ox+d/length*ww,oy+hh-(z-zmin)/(zmax-zmin)*hh) for d,z in zip(distances,samples)]
    dr.polygon([(ox,oy+hh)]+curve+[(ox+ww,oy+hh)],fill=(220,196,150));dr.line(curve,fill=(100,75,44),width=3)
    for i,d in enumerate(nodes):
        px=ox+d/length*ww;z=sample(H,*points[i]);py=oy+hh-(z-zmin)/(zmax-zmin)*hh
        dr.ellipse((px-4,py-4,px+4,py+4),fill=(20,80,173));label=marker_labels[i] if marker_labels else str(i+1)
        dr.text((px-8,oy+hh+9),label,font=font(15),fill=(30,57,90))
    dr.text((ox+ww-85,oy+hh+35),f'{length:.1f} m',font=font(17),fill=(50,55,58))
profile_plot((95,80),(1320,250),DEMO,'原教程路线纵剖面：节点 XY、顺序不变',0,8)
profile_plot((95,520),(1320,285),[crest.tolist(),pool.tolist(),gate.tolist()],'黄油瀑布剖面：三段落崖 → 浅潭 → 下行溪槽',-1,18,['原崖顶','原浅潭','原破壁'])
dr.text((90,902),'单位：米。仅离线地形几何检查；门侧封边、洞顶、悬挑与地下活动面需独立网格。',font=font(20),fill=(65,70,78))
canvas.save(P/'Terrain_Profiles.png')

lines=['# 外部高度图设计与导入说明','',
'依据只读 Handoff Originals JSON 落位，在原平面关系上增加纵深；原件修改为 0，原节点 XY 修改为 0。',
'', '## 导入参数', '',
'- 505 × 505，覆盖 500 × 500 米；每格 0.992063492 米。',
'- Landscape Actor 位置 (-25000, -25000, 0) cm；Scale (99.2063492, 99.2063492, 100)。',
'- R16 为 unsigned 16-bit little endian。数值 = round(32768 + 高程米 × 128)。',
'- HeightRG.png：R 高字节、G 低字节；无 sRGB、最近点采样、无 MipMap，用 landscape_import_heightmap_from_render_target 的 RG 编码模式。',
'- 第 0 行对应原地图 y=0 / Unreal Y=-25000；不要转置、旋转或翻转。',
'', '## 地形构思', '',
'区域以人工设计的不规则岸缘、宽缓活动面和连续进出坡组织。基地是延伸至原储物点的不规则密实台地；主活动面约 6.4 米，设施周边留连续平地。南侧教学路径从约 1.15 米渐升至基地。',
'发酵与酵母区有连续曲折封边切沟。切沟本身并不证明强制入口不可绕行；depth_supplement 的闭合折线须加真实立面和连续碰撞，仅在原破壁与哨戒 XY 留口。瀑布为三段落崖，潭和溪流从 0.65 米水面降到 0.20 米，不能用长斜面替代瀑布体积。',
'霉菌区为约 -4.4 米的连续低地，保留独立绕行走廊。孔洞仅在原坐标产生接收内壁的局部凹槽，仍需真实内壁、孔缘和洞顶网格。世界深层原 XY 不变，新增全局 Z=-60 米活动面；地表右上仅为正高程封闭背景，未生成露天 Boss 盆地。',
'', '## 离线验证', '',
f"- 高程范围 {H.min():.3f} ～ {H.max():.3f} 米。16-bit 量化最大误差 {report['quantisation_error_max_m']:.5f} 米。",
f"- 教程 8 段：纵向最大坡度 {max(r['max_longitudinal_slope_deg'] for r in route_report):.3f}°；道路中心表面最大坡度 {max(r['max_surface_slope_deg'] for r in route_report):.3f}°。",
f"- 三设施周边各 14×14 米检查，最大坡度 {max(r['max_slope_deg'] for r in facility_report):.3f}°。",
'- 三个非共线锚点进行米制→Unreal厘米→米制往返，误差记载在 terrain_validation.json。',
'- 以上是离线数值几何验证，不能替代 UE 固定镜头、碰撞、导航、入口封边及建造试摆测试。',
'', '## 不满足项 / 必须交由工程验证', '',
f"离线硬约束未通过项数量：{len(violations)}。详见 terrain_validation.json 的 unmet_constraints。",
'真实孔洞/洞顶、瀑布落水体、门控碰撞、地下关卡、模块、相机渐隐和玩法不由高度图生成器实现。 secondary_path_reports 保留辅助通行线坡度，个别与地标/孔边相交段必须按原语义沿岸步行或由实际桥体承载，不可把高度图孔内当作道路。',
'', '## 源差异处理', '',
'PNG 的第 9 点是西侧酵母接触，JSON 第 9 点为 (266.7,283.3) 终点；本高度图只沿 JSON 次序生成教程路。flour_demo(200,233.3) 与教程面粉(210,343.3) 为不同来源点。区域轮廓增补体现 PNG 的不规则艺术关系，但不以图像像素替换任何 JSON 坐标。艺术图钉不生成实体。']
(P/'外部高度图设计说明.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print(json.dumps({'files':len(list(P.iterdir())),'min_m':H.min(),'max_m':H.max(),'tutorial':route_report,'facilities':facility_report,'violations':violations},ensure_ascii=False,indent=2))
