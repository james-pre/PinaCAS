<h2 align="center"><b>PinaCAS</b></h2>
<p align="center">
<a href="https://github.com/james-pre/PinaCAS/actions/workflows/ci.yaml" alt="Build Status"><img src="https://github.com/james-pre/PinaCAS/actions/workflows/ci.yaml/badge.svg"></a>
</p>
<p>
PinaCAS is a computer algebra system for the TI-84 Plus CE calculators that shows its work. It is a fork of <a href="https://github.com/nathanfarlow/PineappleCAS">PineappleCAS</a> by Nathan Farlow, which provides the simplifier, identities, derivatives, and the calculator GUI. PinaCAS adds integration and differential equations, in addition to aforementioned showing of work. Both use the <a href="https://github.com/creachadair/imath">imath library</a> for arbitrary precision math.
</p>

## Changes from PineappleCAS

- Indefinite integration: linearity, a table of elementary integrals, polynomial division, u-substitution, and integration by parts
- Step-by-step work for derivatives and integrals, shown in a scrollable viewer with fractions, exponents, roots, and integral signs drawn as in print
- Differential equations entered with primes, such as `Y''+2Y'=3X`, classified by order and linearity
- Checking that a function solves a differential equation and its initial conditions
- Solving first order equations (separable, linear, exact, Bernoulli, homogeneous, and y' = f(ax + by)), linear equations with constant coefficients by undetermined coefficients or variation of parameters, and second order equations by reduction of order, with initial value problems
- Integral and Solve DE functions in the GUI
- Much faster simplification, especially when showing work
- Runs as a Flash app named PinaCAS, so it is not limited to the 64 KB size of a program, and uses spare RAM for a larger heap
- The TI-Basic interface is removed
- Builds with the LLVM-based CE C toolchain, with imath and app_tools as git submodules

## Entering differential equations

Type the equation into a Y= variable or string. Write derivatives of the unknown function with primes from the ANGLE menu (2nd, APPS), and the equals sign from the TEST menu (2nd, MATH). For example, y'' + 16y = 0 is `Y''+16Y=0`. Choose the independent variable with "Respect to", which is X by default. Any letter can be the unknown function, so x'' + w²x = F cos(gt) is `X''+W²X=Fcos(GT)` with respect to T.

Add initial conditions after the equation, separated by commas: y'' + 16y = 0, y(0) = 2, y'(0) = -2 is `Y''+16Y=0,Y(0)=2,Y'(0)=-2`. Enter a differential form M dx + N dy = 0 divided by dx, as `M+NY'=0`.

Solve DE classifies the equation and solves it when it can, writing the solution to the output variable. First order equations are solved by separating variables, with an integrating factor when linear, as exact equations (with an integrating factor of x or y alone when needed), or by the Bernoulli, homogeneous, or y' = f(ax + by) substitutions. An initial condition determines the constant, and the solution is solved for the function when each inverse step is unambiguous, so `Y'=X²e^(6Y),Y(8)=0` gives `-ln(1025-2X³)/6`. Otherwise the implicit solution is written as an equation, like `Y²=X²+C`.

Linear equations with constant coefficients are solved from the roots of the characteristic equation, such as `Y''+16Y=0` giving `Acos(4X)+Bsin(4X)`. The arbitrary constants are A, B, C, and so on, skipping letters in the equation, and initial conditions determine them. A coefficient may be a letter when the equation has the form `X''+W²X=…`, which is taken as an oscillator with W positive. When the right side is not zero, a particular solution comes from undetermined coefficients if each term is a polynomial times e^(ax) times cos(bx) or sin(bx), and otherwise, for second order equations, from variation of parameters. To use reduction of order on a homogeneous second order linear equation, add a known solution after the equation: `4X²Y''+Y=0,Y=√(X)ln(X)`. Equations it cannot solve yet are classified, and linear ones are written in standard form.

To check a solution, put it in another variable, either as `Y=2cos(4X)-1/2sin(4X)` or just `2cos(4X)-1/2sin(4X)`. On the Solve DE page, check "Verify solution" and choose that variable under "Solution in". PinaCAS differentiates the solution, substitutes it into both sides of the equation, and checks each initial condition.

<hr>

## Screenshots

![Main screen](img/gui.png 'GUI')
![Integration by substitution](img/integral-substitution.png 'Integration by substitution, showing work')
![Integration by parts](img/integral-parts.png 'Integration by parts, showing work')
![Derivative](img/derivative.png 'Derivative, showing work')
![Differential equation](img/de.png 'Classifying a differential equation')

These are from PineappleCAS, and show the simplifier.

![Complex example](img/i^i.png 'Complex simplification')
![Trig example](img/trig.png 'Trig identity simplification')
![Exponent example](img/eval_exponent.png 'Large exponent')
![Factorial example](img/eval_factorial.png 'Large factorial')
![Expand example](img/expand.png 'Expand expression')

# Installation

PinaCAS is a Flash app. The calculator only accepts apps signed by TI over the link cable, so PinaCAS comes with an installer program that writes the app from AppVars, like Cesium's installer.

1. Download the latest release and send `PINACAS.8xp` and the `PinaCAS.*.8xv` AppVars to your calculator, or send `PinaCAS.b84`, which bundles them.
2. Send the C libraries, `clibs.8xg`, from the release or from https://github.com/CE-Programming/libraries/releases.
3. Run `PINACAS`. It installs the app and offers to delete the installer files.
4. Open PinaCAS from the APPS menu. Navigate the GUI with the arrow keys, press enter on GUI elements to change their value, and press clear to exit.

On OS 5.5 or newer, TI removed the ability to run assembly programs, so run the installer from a shell like [Cesium](https://github.com/mateoconlechuga/cesium) after jailbreaking with [arTIfiCE](https://yvantt.github.io/arTIfiCE/). On OS 5.3 and 5.4, run `prgmPINACAS` from the home screen, and on older versions run `Asm(prgmPINACAS)`.

To update, first delete the installed app in Mem Management (2nd, +, 2, then Apps), since the installer does not replace it.

# Build

Download and install the latest CE C toolchain from https://github.com/CE-Programming/toolchain

On RHEL-like distributions (e.g. Fedora), `scripts/install-toolchain.sh` installs or updates it in `/opt/CEdev` (override with `CEDEV_PREFIX`), along with the matching `clibs.8xg` for the calculator.

**Compile for calculator:**

```
git clone --recursive https://github.com/james-pre/PinaCAS
cd PinaCAS
make
```

This builds the app, `bin/PinaCAS.8ek`, splits it into the `bin/PinaCAS.*.8xv` AppVars, builds the installer `bin/PINACAS.8xp` from [app_tools](https://github.com/commandblockguy/app_tools), and bundles them into `bin/PinaCAS.b84`. `make debug` builds the same files without optimization into `bin/debug`, with `dbg_printf` output shown in CEmu's console.

The calculator program compiles consistently on Ubuntu, but Windows has a problem with it. Executing make on Windows many times seems to work for some reason. (I blame the compiler!)

**Run in CEmu:**

```
scripts/emu.sh --libs
```

Builds PinaCAS, starts [CEmu](https://ce-programming.github.io/CEmu/) with the ROM at `tmp/TI84+CE.rom` (override with `CEMU_ROM`), sends the installer and AppVars, and runs the installer through Cesium. `--debug` uses the debug build, and `--libs` also sends `clibs.8xg`, which is only needed once. Set `CEMU_CESIUM_APP` to Cesium's number in the APPS menu if it is not 4. Delete the installed app before reinstalling, as on a calculator.

**Compile for PC:**

```
git clone --recursive https://github.com/james-pre/PinaCAS
cd PinaCAS
make pc
```

This builds `bin/pinacas` and does not need the CE toolchain. `make check` runs the tests in `tests.txt`, and `make format` formats the source with clang-format.

### Version

The version shown on the calculator comes from the latest `vX.Y.Z` git tag. Builds after a tag add the commit count and hash as build metadata, like `2.0.0+5.g1a2b3c4`.

## Credits

- James Prevett: PinaCAS
- Nathan Farlow: PineappleCAS, which PinaCAS is built on
- Michael J. Fromberger: the imath library
- commandblockguy: app_tools, which installs the app
- Adriweb and Mateo: help and contributions to PineappleCAS

## Licensing

PineappleCAS is licensed under the MIT, all copyright belongs to Nathan for his amazing work on the original project.
This fork is licensed under the GPL.
