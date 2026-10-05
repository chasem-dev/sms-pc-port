# Run by gdb in the actual port; tests real plaza trees and inventory.
# Relocates Steve next to a real tree so testing does not depend on a route.
import gdb
state = {'frame': 0, 'ready': False, 'tree': 0, 'phase': 0, 'time': 0}

def val(s): return gdb.parse_and_eval(s)
def put(s, v): gdb.execute('set var %s = %s' % (s, v), to_string=True)
def log(s): print('minecraft-test: ' + s, flush=True)
def fail(s):
    log('FAIL ' + s)
    gdb.execute('kill', to_string=True)
    return True

class Update(gdb.Breakpoint):
    def stop(self):
        try:
            if not int(val('gpMarDirector')) or int(val('gpMarDirector->mMap')) != 1: return False
            if int(val('gpMarDirector->mState')) != 4 or int(val('gpMarDirector->unk124')) != 0: return False
            if int(val('treeCount')) == 0: return False
            state['frame'] += 1
            if state['frame'] < 240: return False
            if not state['ready']:
                state['ready'] = True
                state['water']=int(val('gpMarioOriginal->mWaterGun->mCurrentWater'));state['sprayed']=False
                log('plaza ready; %d real palms registered' % int(val('treeCount')))
                p=val('gpMarioOriginal->mPosition')
                candidates=[]
                for i in range(int(val('treeCount'))):
                    t=val('trees[%d].actor->mPosition'%i)
                    candidates.append(((float(t['x'])-float(p['x']))**2+(float(t['z'])-float(p['z']))**2,i))
                _, state['tree'] = min(candidates)
                t=val('trees[%d].actor->mPosition'%state['tree'])
                state['xyz']=[float(t[k]) for k in ('x','y','z')]
                x,y,z=state['xyz']
                put('gpMarioOriginal->mPosition.x',x);put('gpMarioOriginal->mPosition.y',y)
                put('gpMarioOriginal->mPosition.z',z-150)
                put('gpMarioOriginal->mFaceAngle.y',0);put('gpMarioOriginal->mModelFaceAngle',0)
                # Wrong tool must leave the tree intact.
                put('game.inventory.selected',2);put('cooldown',0)
                state['time']=state['frame']
                log('diamond sword equipped; axe required for chopping')
            if int(val('gpMarioOriginal->mWaterGun->mCurrentWater')) < state['water']:state['sprayed']=True
            elapsed=state['frame']-state['time']
            i=state['tree']
            if state['phase']==0 and elapsed>30:
                if bool(val('trees[%d].felled'%i)) or int(val('treeBreak.target'))>=0: return fail('sword damaged tree')
                put('game.inventory.selected',1);put('cooldown',0)
                state['phase']=1;state['time']=state['frame']
                log('wrong-tool check passed; iron axe equipped')
            elif state['phase']==1 and bool(val('trees[%d].felled'%i)):
                put('attackHeld','false')
                if elapsed<35:return fail('palm broke before a continuous hold completed')
                d=sum(bool(val('drops[%d].active'%j)) for j in range(512))
                if d!=4: return fail('expected four visible log drops, got %d'%d)
                actor=val('trees[%d].actor'%i)
                if not int(actor['mLiveFlag']) & 1: return fail('tree still alive')
                log('continuous axe hold felled real palm; four world log pickups spawned')
                state['phase']=2;state['time']=state['frame']
            elif state['phase']==2 and elapsed>60:
                x,y,z=state['xyz']
                put('gpMarioOriginal->mPosition.x',x);put('gpMarioOriginal->mPosition.y',y)
                put('gpMarioOriginal->mPosition.z',z)
                state['phase']=3;state['time']=state['frame']
            elif state['phase']==3 and elapsed>30:
                logs=sum(int(val('game.inventory.slots[%d].count'%j)) for j in range(9) if int(val('game.inventory.slots[%d].item'%j))==4)
                if logs != 4: return fail('expected four collected logs, got %d'%logs)
                log('PASS real tree chopping, tree removal, world drops, proximity pickup, inventory stack=4')
                put('game.inventory.selected',2)
                state['phase']=4;state['time']=state['frame'];state['input_checks']=set()
            elif state['phase']==4:
                field=int(val("'(anonymous namespace)::g_retrace_count'"))
                checks=[(4854,4859,8,None,'number key 9'),(4864,4869,0,None,'wheel wraps 9 to 1'),
                        (4874,4879,1,None,'wheel selects iron axe'),(4884,4889,1,True,'inventory opens'),
                        (4904,4909,1,False,'inventory closes'),(4914,4919,2,None,'diamond sword reselected')]
                for lo,hi,slot,bag,label in checks:
                    if lo<=field<=hi and label not in state['input_checks']:
                        if int(val('game.inventory.selected'))!=slot:return fail(label)
                        if bag is not None and bool(val('game.menu'))!=bag:return fail(label)
                        state['input_checks'].add(label);log('PASS live input route: '+label)
                if field<4960:return False
                if len(state['input_checks'])!=len(checks):return fail('missed scripted input checks')
                # Leave time for runtime screenshots of the held sword and log stack.
                if not state['sprayed']:return fail('FLUDD did not use water during scripted R press')
                log('PASS Steve retains functioning FLUDD; water consumed during scripted spray')
                log('PASS sword equipped during follow-up frames')
                gdb.execute('kill',to_string=True)
                return True
            if state['frame']>1600: return fail('test timed out')
        except Exception as e: return fail(str(e))
        return False
Update('sms_minecraft_update')
