#version 410

#define NO_INTERSECTION intersection(0, material(0, 0, 0, vec3(0), vec3(0)), vec3(0), vec3(0), false)
#define MAX_BOUNCE 128

#define MAX_TRIS_PER_MESH 4098

uniform sampler2D colorMap;
uniform bool hasTexCoords;
uniform bool useMaterial;

in vec3 fragPosition;
in vec3 fragNormal;
in vec2 fragTexCoord;

struct material {
	float specular;
	float diffuse;
	float refract;
	vec3 color;
	vec3 emission;
};

struct camera {
	vec3 pos;

	/* Screen plane positions */
	vec3 p0;
	vec3 p1;
	vec3 p2;

	bool is_moving;
};

struct sphere {
	material mat;
	vec3 pos;
	float r;
};

struct plane {
	material mat;
	vec3 normal;
	float distance;
};

struct triangle {
	material mat;
	vec3 v0;
	vec3 v1;
	vec3 v2;
	vec3 normal;
};

struct mesh {
	int first_tri; // index of first triangle in the TrianglesBuffer
	int tri_count; // number of triangles in the mesh
};

struct light {
	vec3 point;
	vec3 color;
	float intensity;
	float r;
};

struct intersection {
	float distance;
	material mat;
	vec3 normal;
	vec3 point;

	bool hits;
};

struct scene {
	vec2 screen;
	bool debug;
	int stochastic_samples;
	camera cam;
};

struct bvh_node {
	vec3 min;
	vec3 max;
	int left; // index of left child node
	int right;
	int first; // index of first triangle in the TrianglesBuffer
	int count; // number of triangles in this leaf
};

uint seed;

uniform samplerCube skybox;

uniform int num_triangles;
uniform int num_bvh_nodes;

layout (std430) buffer TrianglesBuffer {
	triangle triangles[];
};

layout (std430) buffer BVHBuffer {
	bvh_node bvh_nodes[];
};

layout (std140) uniform ubscene {
	scene sc;
};

layout(location = 0) out vec4 fragColor;

float get_random_numbers(inout uint seed) {
    seed = 1664525u * seed + 1013904223u;
    seed += 1664525u * seed;
    seed ^= (seed >> 16u);
    seed += 1664525u * seed;
    seed ^= (seed >> 16u);
    return seed * pow(0.5, 32.0);
}

vec3 uniform_hemisphere(float u1, float u2) {
	float z = 1 - 2*u1;
	float r = sqrt(max(0, 1 - z*z));
	float phi = TWOPI * u2;
	float x = r * cos(phi);
	float y = r * sin(phi);

	return vec3(x, y, z);
}

intersection intersect(triangle t, vec3 E, vec3 D, float mint, float maxt) {
	const float EPS = EPSILON;
	intersection s = NO_INTERSECTION;

	vec3 v0 = t.v0;
	vec3 v1 = t.v1;
	vec3 v2 = t.v2;

	vec3 edge1 = v1 - v0;
	vec3 edge2 = v2 - v0;

	vec3 h = cross(D, edge2);
	float a = dot(edge1, h);

	if (abs(a) < EPS) {
		return s; // ray parallel to triangle
	}

	float f = 1.0 / a;
	vec3 svec = E - v0;
	float u = f * dot(svec, h);
	if (u < 0.0 || u > 1.0) {
		return s;
	}

	vec3 q = cross(svec, edge1);
	float v = f * dot(D, q);
	if (v < 0.0 || u + v > 1.0) {
		return s;
	}

	float tparam = f * dot(edge2, q);
	if (tparam > mint && tparam < maxt) {
		vec3 point = E + tparam * D;

		s.distance = tparam;
		s.mat = t.mat;
		// Compute geometric normal (triangle may provide cached normal)
		vec3 n = t.normal;
		if(length(n) < EPS) {
			n = normalize(cross(edge1, edge2));
		} else {
			n = normalize(n);
		}
		// Ensure normal faces against the incoming ray
		if (dot(n, D) > 0.0) {
			n = -n;
		}
		s.normal = n;
		s.point = point;
		s.hits = true;
	}

	return s;
}
bool intersectAABB(vec3 bmin, vec3 bmax, vec3 E, vec3 D, float mint, float maxt) {
	float tmin = mint;
	float tmax = maxt;

	for (int i = 0; i < 3; ++i) {
		float invD = 1.0 / D[i];
		float t0 = (bmin[i] - E[i]) * invD;
		float t1 = (bmax[i] - E[i]) * invD;
		if (invD < 0.0) {
			// swap
			float tmp = t0; t0 = t1; t1 = tmp;
		}
		tmin = max(tmin, t0);
		tmax = min(tmax, t1);
		if (tmax < tmin) return false;
	}
	return true;
}

intersection intersect(vec3 E, vec3 D, float mint, float maxt) {
	intersection s = NO_INTERSECTION;

	const int STACK_SIZE = 128;
	int stack[STACK_SIZE];
	int sp = 0;
	stack[sp++] = 0; // push root node

	float currentClosest = maxt;

	while (sp > 0) {
		int node_index = stack[--sp]; // pop
		bvh_node current_node = bvh_nodes[node_index];

		if (!intersectAABB(current_node.min, current_node.max, E, D, mint, min(currentClosest, maxt))) {
			continue;
		}

		if (node.count > 0) {
			// Leaf node: test triangles
			int first = current_node.first;
			int last = first + current_node.count;

			for (int i = first; i < last; i++) {
				intersection t = intersect(triangles[i], E, D, mint, maxt);
				if (t.hits) {
					if (!s.hits || (t.distance < s.distance && t.distance > EPSILON)) {
						s = t;
						currentClosest = t.distance;
					}
				}
			}
		} else {
			// Push nodes

			int left = current_node.left;
			int right = current_node.right;

			if (right >= 0) {
				if (sp < STACK_SIZE) stack[sp++] = right;
			}

			if (left >= 0) {
				if (sp < STACK_SIZE) stack[sp++] = left;
			}
		}
	}

	return s;
}

vec3 specular_step(inout intersection s, inout vec3 origin, inout vec3 spec) {
	vec3 L = normalize(s.point - origin);
	vec3 E = origin;

	vec3 diffuse = vec3(0);
	vec3 col = vec3(0);

	vec3 R = reflect(L, normalize(s.normal));


	//vec3 diffusedir = normalize(vec3(rand(seed) - 0.5, rand(seed) - 0.5, rand(seed) - 0.5));
	vec3 diffusedir = normalize(uniform_hemisphere(rand(seed), rand(seed)));

	if(dot(diffusedir, s.normal) < 0) {
		diffusedir = -diffusedir;
	}

	float focus = dot(s.normal, diffusedir);
	spec *= s.mat.color * focus;

	bool specular_bounce = rand(seed) <= 1 - s.mat.diffuse;

	if(specular_bounce) {
		R = mix(diffusedir, R, s.mat.specular);
	} else {
		R = diffusedir;
	}

	E = s.point;
	intersection t = intersect(E, R, EPSILON, 1e9);
	if(!t.hits) {
		col += texture(skybox, R).rgb * spec;
	}

	s = t;
	origin = E;

	return col;
}

float fresnel(float n1, float n2, vec3 I, vec3 N) {
	float R0 = (n1 - n2) / (n1 + n2); R0 *= R0;
	float costheta = -dot(I, N);
	float x = 1 - costheta;

	float R = R0 + (1-R0)*x*x*x*x*x;

	return R;
}

/* Compute a single refraction bounce/step */
vec3 refraction_step(inout intersection s, inout vec3 origin, inout float refl_mult, int max_depth) {
	vec3 col = vec3(0);

	intersection st, sm;

	float base_mult = refl_mult;

	vec3 D = normalize(s.point - origin);
	vec3 N = normalize(s.normal);

	bool inside = dot(D, N) > 0;

	if(inside) {
		N = -N;
	}

	float n1 = 1;
	float n2 = s.mat.refract;

	float refl;

	if(inside) {
		refl = fresnel(n2, n1, D, N);
	} else {
		refl = fresnel(n1, n2, D, N);
	}

	vec3 R;
	vec3 Rf = refract(D, N, inside ? s.mat.refract : 1 / s.mat.refract);
	vec3 Rm = reflect(D, N);

	/* transmitted */
	st = intersect(s.point, Rf, EPSILON, 1e9);

	/* reflected */
	sm = intersect(s.point, Rm, EPSILON, 1e9);

	bool refl_bounce = rand(seed) <= refl;


	origin = s.point;
	refl_mult *= (1 - refl);

	if(refl_bounce) {
		R = Rm;
		s = sm;
	} else {
		R = Rf;
		s = st;
	}

	if(!s.hits) {
		col = texture(skybox, R).rgb;
	}

	return col;
}

vec3 render() {
	vec3 col = vec3(0, 0, 0);
	vec2 pixel = gl_FragCoord.xy;

	if(sc.debug) {
		pixel.x -= sc.screen.x / 2;
	}

	float r1 = rand(seed) - 0.5;
	float r2 = rand(seed) - 0.5;

	vec2 jitter = vec2(2*r1, 2*r2);

	//jitter /= pixel * 0.5;

	vec3 E = sc.cam.pos;

	vec3 p0 = sc.cam.p0;
	vec3 p1 = sc.cam.p1;
	vec3 p2 = sc.cam.p2;

	vec3 u = (p1 - p0);
	vec3 v = (p2 - p0);

	pixel += jitter;

	float a = pixel.x / (sc.screen.x / (sc.debug ? 2 : 1));
	float b = pixel.y / sc.screen.y;

	vec3 p = p0 + a*u + b*v;

	vec3 D = normalize(p - E);

	intersection s = intersect(E, D, EPSILON, 1e9);

	if(!s.hits) {
		return vec3(texture(skybox, D).rgb);
	}

	float reflect_mult = 1;
	vec3 mask = vec3(1);
	for(int bounce = 0; bounce < MAX_BOUNCE; bounce++) {
		if(!s.hits) {
			break;
		}

		col += s.mat.emission * mask;

		if(s.mat.refract > 0) {
			col += refraction_step(s, E, reflect_mult, 2) * mask;
		} else {
			col += specular_step(s, E, mask);
		}
	}

	//col = clamp(col, vec3(0, 0, 0), vec3(1, 1, 1));

	/* Color of the pixel to return */
	return col;
}

void main()
{
	vec2 coord = gl_FragCoord.xy;

	uint idx = uint(coord.x + coord.y * sc.screen.x);
	seed = idx * uint(accum_sample);

	vec3 accumCol = texture(accum, uv).xyz;

	/* clear the accumulated color if we're moving the camera */
	if(sc.cam.is_moving) {
		accumCol = vec3(0);
	}

	fragColor = render() + accumCol;

    //vec3 normal = normalize(fragNormal);

    //if (hasTexCoords)       { fragColor = vec4(texture(colorMap, fragTexCoord).rgb, 1);}
    //else if (useMaterial)   { fragColor = vec4(kd, 1);}
    //else                    { fragColor = vec4(normal, 1); } // Output color value, change from (1, 0, 0) to something else
}
