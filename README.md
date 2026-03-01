# step-vsthost

Standalone JUCE plugin project for a step sequencer instrument host with Monome grid support.

## Features

- Embedded step sequencer engine
- Hosted instrument loading (VST3, and AU on macOS)
- Monome grid input and LED feedback
- Per-step velocity/probability/pitch/pan/decay lanes

## Requirements

- CMake 3.22+
- C++17 toolchain
- JUCE source tree checked out at `./JUCE`

## Setup

```bash
git clone https://github.com/juce-framework/JUCE.git JUCE
make configure
make build
```

Artifacts are emitted under `Build/step_vsthost_artefacts/`.

## GitHub Publish

```bash
git init
git add .
git commit -m "Initial step-vsthost project"
git branch -M main
git remote add origin <your-github-repo-url>
git push -u origin main
```
