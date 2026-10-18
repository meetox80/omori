const fs = require('fs');
const path = require('path');
const vm = require('vm');
const [, , gameDir, inFile, outFile] = process.argv;
const input = JSON.parse(fs.readFileSync(inFile, 'utf8'));
const stubTarget = function () {};
const stub = new Proxy(stubTarget, {
  get: (t, k) => {
    if (k === Symbol.toPrimitive) return (hint) => (hint === 'number' ? NaN : '[object Object]');
    if (typeof k === 'symbol') return undefined;
    return stub;
  },
  set: () => true,
  apply: () => stub,
  construct: () => stub,
});
const ctx = vm.createContext({});
function run(code, name) {
  for (let tries = 0; tries < 50; tries++) {
    try {
      return vm.runInContext(code, ctx, { filename: name });
    } catch (e) {
      const m = /^(\S+) is not defined$/.exec(e && e.message);
      if (e && e.name === 'ReferenceError' && m) {
        ctx[m[1]] = stub;
        continue;
      }
      throw e;
    }
  }
}
const prelude = fs.readFileSync(path.join(__dirname, '../builder/src/dbproc.cpp'), 'utf8').split('R"JS(')[1].split(')JS"')[0];
ctx.__stub = stub;
run(prelude.replace('var window = this;', 'var window = __stub;'), 'prelude');
run(`
String.prototype.format = function() { var args = arguments; return this.replace(/%([0-9]+)/g, function(s, n) { return args[Number(n) - 1]; }); };
String.prototype.contains = function(s) { return this.indexOf(s) >= 0; };
String.prototype.padZero = function(length){ var s = this; while (s.length < length) { s = '0' + s; } return s; };
Number.prototype.clamp = function(min, max) { return Math.min(Math.max(this, min), max); };
Number.prototype.mod = function(n) { return ((this % n) + n) % n; };
Number.prototype.padZero = function(length){ return String(this).padZero(length); };
Object.defineProperty(Array.prototype, 'contains', { enumerable: false, value: function(e) { return this.indexOf(e) >= 0; } });
Object.defineProperty(Array.prototype, 'clone', { enumerable: false, value: function() { return this.slice(0); } });
Object.defineProperty(Array.prototype, 'equals', { enumerable: false, value: function(array) { if (!array || this.length !== array.length) return false; for (var i = 0; i < this.length; i++) { if (this[i] instanceof Array && array[i] instanceof Array) { if (!this[i].equals(array[i])) return false; } else if (this[i] !== array[i]) return false; } return true; } });
Math.randomInt = function(max) { return Math.floor(max * Math.random()); };
`, 'mvcore');
ctx.__in = input;
run(`var $plugins = JSON.parse(JSON.stringify(__in['$plugins']));
for (var i = 0; i < $plugins.length; i++) if ($plugins[i].status) PluginManager.setParameters($plugins[i].name, $plugins[i].parameters);`, 'plugins');
let failed = 0;
for (const name of Object.keys(input['$ranges'])) {
  const buf = fs.readFileSync(path.join(gameDir, 'js/plugins', name + '.js'));
  for (const [a, b] of input['$ranges'][name]) {
    try { run(buf.slice(a, b).toString('utf8'), name); } catch (e) { failed++; }
  }
}
run(`
$dataActors = JSON.parse(JSON.stringify(__in.actors)); $dataClasses = JSON.parse(JSON.stringify(__in.classes)); $dataSkills = JSON.parse(JSON.stringify(__in.skills));
$dataItems = JSON.parse(JSON.stringify(__in.items)); $dataWeapons = JSON.parse(JSON.stringify(__in.weapons)); $dataArmors = JSON.parse(JSON.stringify(__in.armors));
$dataEnemies = JSON.parse(JSON.stringify(__in.enemies)); $dataTroops = JSON.parse(JSON.stringify(__in.troops)); $dataStates = JSON.parse(JSON.stringify(__in.states));
$dataAnimations = JSON.parse(JSON.stringify(__in.animations)); $dataTilesets = JSON.parse(JSON.stringify(__in.tilesets)); $dataCommonEvents = JSON.parse(JSON.stringify(__in.commonEvents));
$dataSystem = JSON.parse(JSON.stringify(__in.system)); $dataMapInfos = JSON.parse(JSON.stringify(__in.mapInfos));
DataManager.isDatabaseLoaded();
`, 'load');
const out = run(`JSON.stringify({actors:$dataActors,classes:$dataClasses,skills:$dataSkills,items:$dataItems,weapons:$dataWeapons,armors:$dataArmors,enemies:$dataEnemies,troops:$dataTroops,states:$dataStates,animations:$dataAnimations,tilesets:$dataTilesets,commonEvents:$dataCommonEvents,system:$dataSystem,mapInfos:$dataMapInfos,Yanfly:Yanfly})`, 'out');
fs.writeFileSync(outFile, out);
console.error('statements failed:', failed);
