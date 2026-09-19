"""One-speed cold-storage play probe with two independent Marigold cards.

Uses normal planting, collection, shopping and shovel rules. Runs in bounded
segments so the controller can inspect screenshots and revise its strategy.
"""
import argparse
import json
from pathlib import Path
from live import LiveClient


def play(output, until, focused_fire=False, deny_income=False):
    client = LiveClient(output)
    state = client.send([{'op': 'observe'}])['state']
    farm = [(0, 4), (4, 4)]
    order = [1, 3, 2, 0, 4]
    log = output / 'marigold_actions.jsonl'
    next_sample = (int(state['coldStorage']['elapsed']) // 30 + 1) * 30
    while state['boardState'] == 'GAME' and not state['coldStorage']['trophySpawned'] and state['coldStorage']['elapsed'] < until:
        ice = state['coldStorage']
        stock, sun = ice['playerIce'], state['sun']
        plants = {(p['row'], p['col']): p for p in state['plants'] if not p['squished'] and p['type'] != 'PLANT_PUMPKINSHELL'}
        shells = {(p['row'], p['col']): p for p in state['plants'] if not p['squished'] and p['type'] == 'PLANT_PUMPKINSHELL'}
        zombies = [z for z in state['zombies'] if z.get('bodyHealth', 0) > 0]
        cards = state['cards']  # preserve both slots with the same gameplayType
        commands = [{'op': 'collect_sun', 'id': s['id']} for s in state.get('suns', []) if not s.get('collected')]
        for row, col in farm:
            if plants.get((row, col), {}).get('type') == 'PLANT_MARIGOLD':
                commands.append({'op': 'player_shovel', 'row': row, 'col': col})
        if deny_income:
            # 中心点击只铲普通层或空南瓜壳；保留炸弹的起爆机会，不提前拆掉健康肉盾。
            expendable = dict(plants)
            expendable.update({cell:p for cell,p in shells.items() if cell not in plants})
            for (row,col),p in expendable.items():
                if p['type'] in ('PLANT_MARIGOLD','PLANT_IMITATER','PLANT_DOOMSHROOM','PLANT_CHERRYBOMB','PLANT_JALAPENO','PLANT_INSTANT_COFFEE'):
                    continue
                threshold = 250 if p['type']=='PLANT_WALLNUT' else 150
                if 0 < p['health'] <= threshold and any(z['row']==row and abs(z['xInt']-(282+80*col))<120 for z in zombies):
                    commands.append({'op':'player_shovel','row':row,'col':col})
        # 订单先占用预算，避免本轮把阳光全花完后才发现无钱补冰。
        if ice['orderIce'] == 0 and stock <= 100:
            emergency = any(z['xInt'] < 750 for z in zombies)
            reserve = (400 if stock >= 60 else (125 if emergency else 0))
            if sun >= 225 + reserve and stock >= 30:
                commands.append({'op':'buy_ice','large':True})
                sun -= 225
            elif stock < 60 and (sun >= 100 + reserve or (stock < 30 and sun >= 100)):
                commands.append({'op':'buy_ice','large':False})
                sun -= 100
        chosen = None

        def attempt(kind, cells, reserve=0):
            nonlocal chosen
            if chosen:
                return
            for card in cards:
                if card['gameplayType'] != 'PLANT_' + kind or not card['ready']:
                    continue
                if sun - card['sunCost'] < reserve or stock < ice['plantCosts'][card['gameplayType']]:
                    continue
                legal = {tuple(v) for v in card['legalCells']}
                for row, col in cells:
                    if (row, col) in legal:
                        chosen = {'op': 'player_plant', 'slot': card['slot'], 'row': row, 'col': col}
                        return

        nearest = sorted(zombies, key=lambda z: z['xInt'])
        sleeping_dooms = [(r,c) for (r,c),p in plants.items() if p['type']=='PLANT_DOOMSHROOM' and p['sleeping']]
        attempt('INSTANT_COFFEE', sleeping_dooms)
        if nearest and nearest[0]['xInt'] < 555:
            row = nearest[0]['row']
            attempt('JALAPENO', [(row, c) for c in range(7, -1, -1) if (row, c) not in farm])
        # Aim cherry bombs at dense threats in front of the wall, not only after the back line falls.
        blast_cells = []
        for row in range(5):
            for col in range(4, 8):
                x = 282 + 80 * col
                victims = [z for z in zombies if abs(z['row']-row) <= 1 and abs(z['xInt']-x) <= 150]
                score = sum(min(1800, z.get('countableExecutionHealth', z['bodyHealth'])) for z in victims)
                if len(victims) >= 4 or (score >= 4000 and any(z['xInt'] < 770 for z in victims)):
                    blast_cells.append((score, row, col))
        attempt('CHERRYBOMB', [(r,c) for _,r,c in sorted(blast_cells, reverse=True)])
        # 白天毁灭菇需要下一次操作接咖啡：同时预留阳光和冰，落点避开贴脸啃食。
        coffee_ready = any(c['gameplayType']=='PLANT_INSTANT_COFFEE' and c['ready'] for c in cards)
        if coffee_ready and not sleeping_dooms and stock >= 35:
            doom_cells = []
            for row in range(1,4):
                for col in range(3,7):
                    if (row,col) in plants or (row,col) in farm:
                        continue
                    x = 282 + 80 * col
                    if any(z['row']==row and abs(z['xInt']-x)<150 for z in zombies):
                        continue
                    victims = [z for z in zombies if (z['xInt']-x)**2 + ((z['row']-row)*100)**2 < 230**2]
                    score = sum(min(1800,z.get('countableExecutionHealth',z['bodyHealth'])) for z in victims)
                    if len(victims)>=5 and score>=5000:
                        doom_cells.append((score,row,col))
            attempt('DOOMSHROOM', [(r,c) for _,r,c in sorted(doom_cells,reverse=True)], 75)
        attempt('MARIGOLD', [v for v in farm if v not in plants])
        row_order = list(dict.fromkeys([z['row'] for z in nearest] + [2,0,4,1,3]))
        attempt('WALLNUT', [(r,5) for r in row_order if (r,5) not in plants])
        producers = sum(p['type'] == 'PLANT_SUNFLOWER' for p in plants.values())
        if producers < 6:
            attempt('SUNFLOWER', [(r,0) for r in [2,0,4,1,3]] + [(2,4)])
        fire_rows = {r for (r,c),p in plants.items() if p['type'] in ('PLANT_MELONPULT','PLANT_WINTERMELON')}
        weak = [r for r in [1,3,0,4,2] if r not in fire_rows]
        weak.sort(key=lambda r: min([z['xInt'] for z in zombies if z['row']==r] or [2000]))
        attempt('POTATOMINE', [(r,6) for r in weak if any(z['row']==r and z['xInt']>1020 for z in zombies)])
        attempt('MELONPULT', [(r,1) for r in weak])
        if len(fire_rows) == 5:
            attempt('WINTERMELON', [(r,1) for r in ([1,3] if focused_fire else order)], 125)
        if len(fire_rows) == 5:
            iced_sides = all(plants.get((r,1),{}).get('type')=='PLANT_WINTERMELON' for r in [1,3])
            if iced_sides and producers < 8:
                attempt('SUNFLOWER', [(r,4) for r in [1,3]])
            damaged = [(r,5) for r in row_order if plants.get((r,5),{}).get('type')=='PLANT_WALLNUT' and plants[(r,5)]['health']<1800 and (r,5) not in shells]
            attempt('PUMPKINSHELL', damaged, 125)
            # 两路冰瓜提供溅射减速后，先补普通西瓜，避免反复升级挤占输出预算。
            if iced_sides and focused_fire:
                attempt('MELONPULT', [(r,2) for r in order], 150)
            if iced_sides:
                attempt('STARFRUIT', [(r,3) for r in [2,1,3,0,4]], 150)
            if iced_sides:
                attempt('MELONPULT', [(r,2) for r in order], 150)
            if not focused_fire:
                attempt('WINTERMELON', [(r,2) for r in order], 150)
            building_fire = focused_fire and any((r,2) not in plants for r in order)
            if iced_sides and not building_fire:
                attempt('PUMPKINSHELL', [(r,1) for r in row_order if (r,1) not in shells], 150)
        if chosen:
            commands.append(chosen)
            sun -= cards[chosen['slot']]['sunCost']
            stock -= ice['plantCosts'][cards[chosen['slot']]['gameplayType']]
        commands.append({'op':'advance','steps':60})  # one game second at 1x
        before = ice['elapsed']
        reply = client.send(commands)
        state = reply['state']
        assert state['coldStorage']['elapsed'] - before < 1.02, 'Unexpected speed above 1x'
        with log.open('a',encoding='utf-8') as f:
            f.write(json.dumps({'time':before,'commands':commands,'results':reply['results']},ensure_ascii=False)+'\n')
        failures = [r for r in reply['results'] if not r.get('ok',True) and r.get('reason') != 'sun_unavailable']
        if failures:
            raise RuntimeError(failures)
        if state['coldStorage']['elapsed'] >= next_sample:
            sample = {k: state['coldStorage'][k] for k in ['elapsed','playerIce','enemyIce','spent','supplied','killIncome','playerKillIncome','deployments']}
            sample.update(sun=state['sun'],plants=state['plantCount'],zombies=state['zombieCount'],wave=state['wave'])
            print(json.dumps(sample),flush=True)
            next_sample += 30
        (output/'marigold_checkpoint.json').write_text(json.dumps(state,ensure_ascii=False),encoding='utf-8')
    elapsed = int(state['coldStorage']['elapsed'])
    client.send([{'op':'screenshot','name':f'marigold_{elapsed}.png'}])
    (output/f'marigold_{elapsed}.json').write_text(json.dumps(state,ensure_ascii=False,indent=2),encoding='utf-8')
    print('Segment:',elapsed,state['boardState'],'win=',state['coldStorage']['trophySpawned'],flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output',type=Path)
    parser.add_argument('--until',type=float,required=True)
    parser.add_argument('--focused-fire',action='store_true',help='Keep two slowing melons and prioritize a second damage column')
    parser.add_argument('--deny-income',action='store_true',help='Shovel critically damaged plants threatened by nearby enemies')
    args=parser.parse_args()
    play(args.output,args.until,args.focused_fire,args.deny_income)
