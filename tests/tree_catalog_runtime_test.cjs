// Exercise the real catalog/controller and Three.js geometry in Node. This
// validates data loading and poses; WebGL rendering still needs visual review.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const directory = path.resolve(process.argv[2] || 'private/freecamera/tree-review');
const html = fs.readFileSync(path.join(directory, 'index.html'), 'utf8');
const scripts = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(x => x[1]);
const config = html.match(/<script id="tree-data" type="application\/json">([\s\S]*?)<\/script>/)[1];
class Element {
  constructor() { this.value='';this.checked=false;this.children=[];this.style={};this.clientWidth=800;this.clientHeight=500;this.textContent=''; }
  add(option) {this.children.push(option);if(this.children.length===1)this.value=option.value;}
  append(...children) {this.children.push(...children);}
  appendChild(child) {this.children.push(child);return child;}
  replaceChildren(...children) {this.children=[];this.value='';for(const child of children)this.add(child);}
  addEventListener() {}
  setPointerCapture() {}
  querySelector() {return this.stats ||= new Element();}
  remove() {}
  click() {}
}
const elements = new Map();
function element(id) {if(!elements.has(id))elements.set(id,new Element());return elements.get(id);}
element('tree-data').textContent=config;
element('textured').checked=true;element('loop').checked=true;element('speed').value='1';element('light').value='35';
const body=new Element();
const document={getElementById:element,createElement:()=>new Element(),createTextNode:text=>({textContent:text}),body};
const context=vm.createContext({document,console,devicePixelRatio:1,requestAnimationFrame(){},
  Option:class{constructor(text,value){this.text=text;this.value=value;}},
  localStorage:{getItem:()=>null,setItem(){}},Blob,URL});
context.window=context;context.addEventListener=()=>{};
vm.runInContext(scripts[0],context);
context.THREE.WebGLRenderer=class{constructor(){this.domElement=new Element();}setPixelRatio(){}setSize(){}render(){}};
context.THREE.TextureLoader.prototype.load=function(){return new context.THREE.Texture();};
body.appendChild=function(script){vm.runInContext(fs.readFileSync(path.join(directory,script.src),'utf8'),context);script.onload();return script;};
vm.runInContext(scripts[1],context);
const manifest=JSON.parse(config);
let models=0,poses=0,weighted=0;
for(let a=0;a<manifest.assets.length;a++){
  vm.runInContext(`selectAsset(${a})`,context);
  const count=vm.runInContext('Object.keys(DATA.before.models).length',context);
  for(let m=0;m<count;m++){
    vm.runInContext(`$('model').value=Object.keys(DATA.before.models)[${m}];loadModel()`,context);
    const clipNames=vm.runInContext('activeModel().clips.map(c=>c.name)',context);
    assert(vm.runInContext(`views.every(v=>v.meshes.every(m=>{
      const face=m.userData.source.displayFace;
      return ['front','back','both','none'].includes(face)&&m.material.side===
        (face==='both'?THREE.DoubleSide:face==='back'?THREE.BackSide:THREE.FrontSide);
    }))`,context),'Every shape must use its own exported material culling');
    vm.runInContext("$('double').checked=true;updateMaterials()",context);
    assert(vm.runInContext('views.every(v=>v.meshes.every(m=>m.material.side===THREE.DoubleSide))',context),'Force backfaces must override all materials');
    vm.runInContext("$('double').checked=false;updateMaterials()",context);
    weighted+=vm.runInContext('activeModel().shapes.some(s=>s.weights)?1:0',context);
    vm.runInContext("$('fruit').value='Peach';updateMaterials()",context);
    for(const name of ['',...clipNames]){
      for(const fraction of (name?[0,.5,1]:[0])){
        vm.runInContext(`$('clip').value=${JSON.stringify(name)};selectClip();frame=currentClip?currentClip.frames*${fraction}:0;pose()`,context);
        const valid=vm.runInContext(`views.every(v=>v.meshes.every(m=>
          Array.from(m.geometry.attributes.position.array).every(Number.isFinite)&&
          Array.from(m.geometry.attributes.normal.array).every(Number.isFinite)))`,context);
        assert(valid,`${manifest.assets[a].file}: invalid pose ${name} at ${fraction}`);
        assert(vm.runInContext(`views.every(v=>v.meshes.every(m=>{
          const s=m.userData.source;
          return !s.fruitSpecies || m.visible===(!currentClip&&s.fruitSpecies==='Peach'&&s.displayFace!=='none');
        }))`,context),'Fruit species selection and clip visibility must match in both panes');
        poses++;
      }
    }
    models++;
  }
}
assert.equal(models,manifest.modelCount);
assert(weighted>0,'Weighted models must be covered');
vm.runInContext("const selectedModel=$('model').value;$('baseline').value='previous';setAsset(DATA,true);if($('model').value!==selectedModel||DATA.before!==DATA.previous)throw Error('Previous review comparison failed');$('baseline').value='stock';setAsset(DATA,true);if(DATA.before!==DATA.stock)throw Error('Stock comparison failed')",context);
vm.runInContext("$('review').value='changes';$('note').value='Check rear canopy';saveReview();refreshReview()",context);
assert.equal(element('review').value,'changes');
assert.equal(element('note').value,'Check rear canopy');
console.log(`Passed ${manifest.assets.length} assets, ${models} models, ${poses} poses, ${weighted} weighted models; review state round trip passed.`);
