
<template>
    <br>
    <h2> Trusted by </h2>
    <a class="enjoyer" href="https://lux.csail.mit.edu/" target="_blank">
        <img width="32" height="32" src="https://raw.githubusercontent.com/LuxDL/Lux.jl/refs/heads/main/assets/lux-logo.svg" />
        <span>
            <p class="extra-info">Scientific Computing</p>
            <p class="heading">Lux.jl</p>
            <p class="extra-info">Machine Learning</p>
        </span>
    </a>
    <a class="enjoyer" href="https://bsc-quantic.github.io/Tenet.jl/stable/" target="_blank">
        <img width="32" height="32" src="https://raw.githubusercontent.com/bsc-quantic/Tenet.jl/refs/heads/master/docs/src/assets/logo.svg" />
        <span>
            <p class="extra-info">Quantum Simulation</p>
            <p class="heading">Tenet.jl</p>
            <p class="extra-info">Tensor Networks</p>
        </span>
    </a>
</template>

<style>
.enjoyer {
    margin-top: 0.5rem;
    margin-bottom: 0.0rem;
    border-radius: 14px;
    padding-top: 0.2rem;
    padding-bottom: 0.2rem;
    position: relative;
    font-size: 0.9rem;
    font-weight: 700;
    line-height: 1.1rem;
    display: flex;
    align-items: center;
    justify-content: center;
    width: 100%;
    gap: 1rem;
    background-color: var(--vp-c-bg-alt);
    border: 2px solid var(--vp-c-bg-alt);
    transition: border-color 0.5s;
}

.enjoyer:hover {
    border: 2px solid var(--vp-c-brand-light);
}

.enjoyer img {
    transition: transform 0.5s;
    transform: scale(1.25);
}

.enjoyer:hover img {
    transform: scale(1.75);
}

.enjoyer .heading {
    background-image: linear-gradient(120deg,
            #b047ff 16%,
            var(--vp-c-brand-lighter),
            var(--vp-c-brand-lighter));
    background-clip: text;
    -webkit-background-clip: text;
    -webkit-text-fill-color: transparent;
}

.enjoyer .extra-info {
    color: var(--vp-c-text-1);
    opacity: 0;
    font-size: 0.7rem;
    padding-left: 0.1rem;
    transition: opacity 0.5s;
}

.enjoyer:hover .extra-info {
    opacity: 0.9;
}
</style>