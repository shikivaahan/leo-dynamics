#!/usr/bin/env python3
"""Six scientific figures from sensitivity-v1.1; no unmeasured reference oracle."""
import argparse
import hashlib
import gzip
import json
import textwrap
from pathlib import Path
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

UNITS={"position_m":"m","attitude_angle_rad":"rad","omega_rad_s":"rad/s",
       "sma_final_m":"m","mean_projected_area_m2":"m²","srp_work_j":"J"}
CHANNELS=tuple(UNITS)
COLOURS=("#0072B2","#D55E00","#009E73")
METHODS={"dopri":("o","DP5"),"rkf78":("s","RKF78"),"rk4":("^","RK4"),"mixed_rk4":("x","mixed RK4")}


def grid(count,columns=3):
    columns=min(columns,count);rows=(count+columns-1)//columns
    fig,axes=plt.subplots(rows,columns,figsize=(5.2*columns,3.8*rows),squeeze=False,layout="constrained")
    flat=list(axes.flat)
    for ax in flat[count:]:ax.set_visible(False)
    return fig,flat[:count]


def heading(fig,text):
    fig.suptitle(textwrap.fill(text,width=max(55,int(fig.get_figwidth()*10))),fontsize=11)


def decorate(ax,title,xlabel,ylabel):
    ax.set_title(title,loc="left",fontsize=11);ax.set_xlabel(xlabel);ax.set_ylabel(ylabel)
    ax.grid(True,which="both",alpha=.2)


def finite_positive(x):return isinstance(x,(int,float)) and x>0


def sensitivity_rows(case,horizon):return [r for r in case["sensitivity"] if r["horizon_s"]==horizon]


def ranked(case,horizon,observable):
    chosen={}
    for row in sensitivity_rows(case,horizon):
        cell=row["observables"][observable]
        if not cell["resolved"]:continue
        name=row["parameter"]
        if name not in chosen or row["delta"]>chosen[name]["delta"]:
            chosen[name]=dict(parameter=name,delta=row["delta"],**cell)
    return sorted(chosen.values(),key=lambda r:-r["response"])


def timestep(cases,horizon):
    keys=("position_m","attitude_angle_rad","omega_rad_s")
    fig,axes=grid(len(cases)*len(keys))
    for i,(name,case) in enumerate(cases.items()):
        row=next(r for r in case["convergence"] if r["horizon_s"]==horizon)
        for j,key in enumerate(keys):
            ax=axes[i*3+j]
            for method,(marker,label) in METHODS.items():
                points=[]
                for sweep in row["sweeps"]:
                    if sweep["label"] in ("rkf78-half","cadence-half"):continue
                    settings=sweep["settings"];actual=settings.get("method","dopri")
                    step=settings.get("max_step");error=sweep["observables"].get(key)
                    if actual==method and finite_positive(step) and finite_positive(error):points.append((step,error))
                points.sort()
                if points:ax.loglog(*zip(*points),marker=marker,linestyle="none" if method=="dopri" else "-",label=label)
            decorate(ax,f"{name}: {key}","prescribed/capped maximum step [s]",f"maximum disagreement [{UNITS[key]}]")
            ax.set_xticks((.25,.5,1,2,5),labels=("0.25","0.5","1","2","5"))
            ax.tick_params(axis="x",which="minor",labelbottom=False)
            if ax.lines:ax.legend(fontsize=8)
    heading(fig,f"Common-time disagreement to RKF78 0.5 s at {horizon} s, not an oracle. Self-comparisons omitted; DP5 points have different tolerance configurations.")
    return fig


def horizons(cases):
    fig,axes=grid(len(CHANNELS))
    for ax,key in zip(axes,CHANNELS):
        for colour,(name,case) in zip(COLOURS,cases.items()):
            points=[(r["horizon_s"],r["uncertainty"].get(key)) for r in case["convergence"]]
            points=[(t,v) for t,v in points if finite_positive(v)]
            if points:ax.loglog(*zip(*points),"o-",color=colour,label=name)
        decorate(ax,key,"propagation horizon [s]",f"empirical numerical envelope [{UNITS[key]}]")
        if ax.lines:ax.legend(fontsize=8)
    heading(fig,"2× fine cross-method/refinement/cadence spread; empirical envelope, not a certified bound")
    return fig


def time_response(cases,horizon):
    parameters=("attitude_orbit","body_rate","cp_y","density")
    fig,axes=grid(len(cases))
    for ax,(name,case) in zip(axes,cases.items()):
        rows=sensitivity_rows(case,horizon)
        for parameter in parameters:
            choices=[r for r in rows if r["parameter"]==parameter]
            if not choices:continue
            row=max(choices,key=lambda r:r["delta"]);cell=row["observables"]["attitude_angle_rad"]
            times=row["response_series"]["time_s"];values=row["response_series"]["attitude_angle_rad"]
            if len(times)!=len(values):raise ValueError("unmatched response samples")
            label=f"{parameter}: +{row['delta']:g} {cell['parameter_unit']} ({'resolved' if cell['resolved'] else 'unresolved'})"
            line,=ax.plot(times,values,"-" if cell["resolved"] else "--",label=label)
            if finite_positive(cell["uncertainty"]):ax.axhline(cell["uncertainty"],color=line.get_color(),linestyle=":",alpha=.5)
        ax.set_yscale("symlog",linthresh=1e-8)
        ax.set_ylim(bottom=0)
        decorate(ax,name,"physical time [s]","SO(3) response [rad]")
        if ax.lines:ax.legend(fontsize=7)
    heading(fig,"Measured +perturbation RKF78 response; dashed = unresolved; dotted = same-channel perturbed numerical envelope")
    return fig


def perturbations(cases,horizon):
    parameters=("attitude_orbit","cp_y","density")
    fig,axes=grid(len(cases)*3)
    for i,(name,case) in enumerate(cases.items()):
        for j,parameter in enumerate(parameters):
            ax=axes[i*3+j];rows=sorted((r for r in sensitivity_rows(case,horizon) if r["parameter"]==parameter),key=lambda r:r["delta"])
            for row in rows:
                cell=row["observables"]["attitude_angle_rad"]
                if finite_positive(cell["response"]):
                    ax.loglog(row["delta"],cell["response"],"o",color="C0",markerfacecolor="C0" if cell["resolved"] else "none")
                if finite_positive(cell["uncertainty"]):ax.loglog(row["delta"],cell["uncertainty"],"x",color="C1")
            unit=rows[0]["observables"]["attitude_angle_rad"]["parameter_unit"] if rows else "undefined"
            decorate(ax,f"{name}: {parameter}",f"signed-pair perturbation magnitude [{unit}]","maximum SO(3) response / envelope [rad]")
    heading(fig,f"{horizon} s: filled response = resolved; hollow = unresolved; × = numerical envelope. Units separated by panel.")
    return fig


def ranking(cases,horizon,observable):
    fig,axes=grid(len(cases))
    for ax,(name,case) in zip(axes,cases.items()):
        rows=ranked(case,horizon,observable)[:7]
        if rows:
            labels=[f"{r['parameter']} ±{r['delta']:g} {r['parameter_unit']}" for r in rows]
            ax.barh(labels,[r["response"] for r in rows],xerr=[r["uncertainty"] for r in rows],capsize=2)
            ax.set_xlim(left=0);ax.invert_yaxis()
        else:ax.text(.5,.5,"No resolved ranking",ha="center",transform=ax.transAxes)
        decorate(ax,name,f"response in {observable} [{UNITS[observable]}]","tested exploratory span")
    heading(fig,f"{horizon} s: same observable and tested spans, not hardware-uncertainty importance. Error bars = numerical envelope; overlapping ranks are not distinguished.")
    return fig


def resolution(cases,horizon):
    fig,axes=grid(len(CHANNELS))
    for ax,key in zip(axes,CHANNELS):
        limits=[];excluded=0
        for colour,(name,case) in zip(COLOURS,cases.items()):
            for resolved,marker in ((True,"o"),(False,"x")):
                points=[]
                for row in sensitivity_rows(case,horizon):
                    cell=row["observables"][key];u,r=cell["uncertainty"],cell["response"]
                    if cell["resolved"]!=resolved:continue
                    if not finite_positive(u) or not finite_positive(r):excluded+=1;continue
                    points.append((u,r));limits.extend((u,r))
                if points:ax.scatter(*zip(*points),s=15,c=colour,marker=marker,alpha=.6,label=f"{name} {'resolved' if resolved else 'unresolved'}")
        if limits:
            bounds=(min(limits),max(limits));ax.loglog(bounds,[10*x for x in bounds],"k:",label="response = 10× envelope")
        ax.set_xscale("log");ax.set_yscale("log")
        decorate(ax,key,f"numerical envelope [{UNITS[key]}]",f"physical response [{UNITS[key]}]")
        ax.text(.03,.03,f"{excluded} zero/undefined cells omitted",transform=ax.transAxes,fontsize=7)
        if ax.has_data():ax.legend(fontsize=6)
    heading(fig,f"{horizon} s: crossing 10× alone is insufficient; method and neighbor-size checks are also mandatory")
    return fig


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("experiments",type=Path);p.add_argument("--output",type=Path,required=True)
    p.add_argument("--format",choices=("png","svg","both"),default="png")
    p.add_argument("--horizon",type=int,default=21600);p.add_argument("--observable",choices=CHANNELS,default="sma_final_m")
    a=p.parse_args();source=json.loads(gzip.decompress(a.experiments.read_bytes()) if a.experiments.suffix==".gz" else a.experiments.read_text());cases=source["cases"]
    a.output.mkdir(parents=True,exist_ok=True)
    builders={"timestep_vs_error":lambda:timestep(cases,a.horizon),"convergence_vs_horizon":lambda:horizons(cases),
              "attitude_sensitivity_vs_time":lambda:time_response(cases,a.horizon),"perturbation_vs_response":lambda:perturbations(cases,a.horizon),
              "parameter_ranking":lambda:ranking(cases,a.horizon,a.observable),"uncertainty_vs_response":lambda:resolution(cases,a.horizon)}
    files=[]
    for name,builder in builders.items():
        fig=builder()
        for ext in (("png","svg") if a.format=="both" else (a.format,)):
            path=a.output/(name+"."+ext);fig.savefig(path,dpi=130);files.append(path.name)
        plt.close(fig);print("Rendered",name,flush=True)
    (a.output/"manifest.json").write_text(json.dumps(dict(source_sha256=hashlib.sha256(a.experiments.read_bytes()).hexdigest(),horizon_s=a.horizon,ranking_observable=a.observable,files=files),indent=2)+"\n")


if __name__=="__main__":main()
