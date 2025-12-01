/**
 * A structure with fields labeled with vertex attribute locations can be used
 * as input to the entry point of a shader.
 */
struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f, // new attribute
    @location(2) color: vec3f,
};

/**
 * A structure with fields labeled with builtins and locations can also be used
 * as *output* of the vertex shader, which is also the input of the fragment
 * shader.
 */
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec3f,
    @location(1) normal: vec3f,
};

/**
 * A structure holding the value of our uniforms
 */
struct MyUniforms {
    projectionMatrix: mat4x4f,
    viewMatrix: mat4x4f,
    modelMatrix: mat4x4f,
    color: vec4f,
    time: f32,
};

// Instead of the simple uTime variable, our uniform variable is a struct
@group(0) @binding(0) var<uniform> uMyUniforms: MyUniforms;

const pi = 3.14159265359;

// Build an orthographic projection matrix
fn makeOrthographicProj(ratio: f32, near: f32, far: f32, scale: f32) -> mat4x4f {
    return transpose(mat4x4f(
        1.0 / scale, 0.0, 0.0, 0.0,
        0.0, ratio / scale, 0.0, 0.0,
        0.0, 0.0, 1.0 / (far - near), -near / (far - near),
        0.0, 0.0, 0.0, 1.0,
    ));
}

// Build a perspective projection matrix
fn makePerspectiveProj(ratio: f32, near: f32, far: f32, focalLength: f32) -> mat4x4f {
    let divides = 1.0 / (far - near);
    return transpose(mat4x4f(
        focalLength, 0.0, 0.0, 0.0,
        0.0, focalLength * ratio, 0.0, 0.0,
        0.0, 0.0, far * divides, -far * near * divides,
        0.0, 0.0, 1.0, 0.0,
    ));
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    out.position = uMyUniforms.projectionMatrix * uMyUniforms.viewMatrix * uMyUniforms.modelMatrix * vec4f(in.position, 1.0);
    out.color = in.color;

    // Forward the normal
    out.normal = (uMyUniforms.modelMatrix * vec4f(in.normal, 0.0)).xyz;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // We multiply the scene's color with our global uniform (this is one
    // possible use of the color uniform, among many others).
    // let color = in.color * uMyUniforms.color.rgb;
    let normal = normalize(in.normal);
    let lightColor1 = vec3f(1.0, 0.9, 0.6);
    let lightColor2 = vec3f(0.6, 0.9, 1.0);
    let lightDirection1 = vec3f(0.5, -0.9, 0.1);
    let lightDirection2 = vec3f(0.2, 0.4, 0.3);
    let shading1 = max(0.0, dot(lightDirection1, normal));
    let shading2 = max(0.0, dot(lightDirection2, normal));
    let shading = shading1 * lightColor1 + shading2 * lightColor2;
    let color = in.color * shading;

    // Gamma-correction
    let linear_color = pow(color, vec3f(2.2));
    return vec4f(linear_color, uMyUniforms.color.a);
}