// Generated only from camera_contract.ROOM_BOXES, no appearance labels.
bool surfaceIncident(vec3 ro, vec3 rd) {
    vec3 boxes[40]=vec3[](
vec3(-576.0,-1600.0,-64.0),
vec3(576.0,-512.0,0.0),
vec3(-576.0,-1600.0,512.0),
vec3(576.0,-512.0,576.0),
vec3(-576.0,-1600.0,0.0),
vec3(-512.0,-512.0,512.0),
vec3(512.0,-1600.0,0.0),
vec3(576.0,-512.0,512.0),
vec3(-576.0,-512.0,-64.0),
vec3(576.0,512.0,0.0),
vec3(-576.0,-512.0,512.0),
vec3(576.0,512.0,576.0),
vec3(-576.0,-512.0,0.0),
vec3(-512.0,512.0,512.0),
vec3(512.0,-512.0,0.0),
vec3(576.0,512.0,512.0),
vec3(-576.0,512.0,-64.0),
vec3(576.0,1600.0,0.0),
vec3(-576.0,512.0,512.0),
vec3(576.0,1600.0,576.0),
vec3(-576.0,512.0,0.0),
vec3(-512.0,1600.0,512.0),
vec3(512.0,512.0,0.0),
vec3(576.0,1600.0,512.0),
vec3(-576.0,-1600.0,0.0),
vec3(576.0,-1536.0,512.0),
vec3(-576.0,1536.0,0.0),
vec3(576.0,1600.0,512.0),
vec3(-576.0,-544.0,0.0),
vec3(-96.0,-512.0,512.0),
vec3(96.0,-544.0,0.0),
vec3(576.0,-512.0,512.0),
vec3(-96.0,-544.0,224.0),
vec3(96.0,-512.0,512.0),
vec3(-576.0,512.0,0.0),
vec3(-96.0,544.0,512.0),
vec3(96.0,512.0,0.0),
vec3(576.0,544.0,512.0),
vec3(-96.0,512.0,224.0),
vec3(96.0,544.0,512.0));
    float wall=100000.0;
    for(int box=0;box<20;box++) {
        vec3 mn=boxes[box*2], mx=boxes[box*2+1];
        float nearT=-100000.0, farT=100000.0; bool valid=true;
        for(int axis=0;axis<3;axis++) {
            bool parallel=abs(rd[axis])<1e-7;
            if(parallel) {if(ro[axis]<mn[axis]||ro[axis]>mx[axis])valid=false;}
            else {
                float t1=(mn[axis]-ro[axis])/rd[axis],t2=(mx[axis]-ro[axis])/rd[axis];
                nearT=max(nearT,min(t1,t2));farT=min(farT,max(t1,t2));
            }
            valid=valid&&nearT<=farT;
        }
        float hit=nearT<=.0001?farT:nearT;
        if(valid&&farT>.0001&&hit>1.0)wall=min(wall,hit);
    }
    if(abs(rd.x)<1e-6)return false;
    float t=(494.0-ro.x)/rd.x;vec3 point=ro+rd*t;
    return t>.0001&&abs(point.y)<=384.0&&point.z>=0.0&&point.z<=432.0&&t<wall-.5;
}
