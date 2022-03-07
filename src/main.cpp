#define _CRT_SECURE_NO_WARNINGS 1
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <vector>
#include <algorithm>
#include <iostream>
#include <cmath>
#include <chrono>
#include "omp.h"

#include "Vector.h"
#include "Ray.h"
#include "Sphere.h"
#include "Scene.h"
#include "RandomHelper.h"
#include "Object.h"
#include "Triangle.h"

#include "progressbar.hpp"

#define INDIRECT_LIGHT true
#define SOFT_SHADOWS true
#define NUM_RAYS_MC 64
#define ANTIALIASING true
#define DEPTH_OF_FIELD false
#define DDOF 55
#define USE_BVH true
#define NORMALS_INTERPOLATION true

int main(int argc, char *argv[])
{
    int W = 512;
    int H = 512;

    Vector C(0, 0, 55);           // Camera
    Vector L(-10, 20, 40);        // Source de lumière
    double fov = 60 * M_PI / 180; // Field of view 60°
    double tanfov2 = tan(fov / 2);
    double I = 5E10;         // Intensité de la lumière (en Watts)
    int nb_rays_monte_carlo; // Nombre de rayons envoyés pour Monte Carlo (error decreases in 1/sqrt(N))
    if (INDIRECT_LIGHT || SOFT_SHADOWS || ANTIALIASING)
        nb_rays_monte_carlo = NUM_RAYS_MC;
    else
        nb_rays_monte_carlo = 1;
    int rebonds = 5;
    double ddof = DDOF;

    RandomHelper randh;

    // Initialisation de la scène
    Scene scene(randh);
    scene.I = I;
    scene.indirect_light = INDIRECT_LIGHT;
    scene.soft_shadows = SOFT_SHADOWS;

    // Initialisation des objets de la scène
    Sphere s1(Vector(0, 0, 0), 10, Vector(0.4, 0.1, 0.), false, false);
    Sphere s2(Vector(-25, 20, -35), 10, Vector(0.4, 0.4, 0.4), false, false);
    Sphere s3(Vector(10, -5, 25), 5, Vector(0.1, 0., 0.5), false, false);

    // ========== Load mesh ==========

    TriangleMesh tri(Vector(0.4, 0.1, 0.1), false, false);

    // Girl
    // tri.readOBJ("mesh/girl/Beautiful Girl.obj");
    // tri.rotate(0, 90);
    // tri.rotate(1, 180);
    // tri.scale(20, Vector(0, -10, 0));

    // Cat
    // tri.readOBJ("mesh/cat/cat.obj");
    // tri.rotate(1, 45);
    // tri.scale(0.05, Vector(0, -10, 0));

    // Dog

    // Sheep
    tri.readOBJ("mesh/sheep/sheep.obj");
    tri.scale(10, Vector(0, -10, 0));
    tri.rotate(1, 135);

    tri.normals_interpolation = NORMALS_INTERPOLATION;
    if (USE_BVH)
    {
        tri.use_bvh = USE_BVH;
        std::cout << "Init BVH..." << std::endl;
        tri.init_BVH();
        std::cout << "Init BVH done" << std::endl;
    }
    else
    {
        tri.use_bvh = USE_BVH;
        tri.get_bbox();
    }

    // Sphere sBack(Vector(0, 0, -1000), 940, Vector(0., 0.5, 0.));   // Sphère derrière la boule
    // Sphere sFront(Vector(0, 0, 1000), 940, Vector(0.5, 0., 0.5));  // Sphère derrière la caméra
    // Sphere sUp(Vector(0, 1000, 0), 940, Vector(0.5, 0., 0.));      // Plafond
    // Sphere sDown(Vector(0, -1000, 0), 990, Vector(0., 0., 0.5));   // Sol
    // Sphere sRight(Vector(1000, 0, 0), 940, Vector(0.5, 0.5, 0.5)); // Mur droit
    // Sphere sLeft(Vector(-1000, 0, 0), 940, Vector(0.5, 0.5, 0.5)); // Mur gauche

    Vector albedo_spheres = Vector(0.4, 0.4, 0.4);

    Sphere sBack(Vector(0, 0, -1000), 940, albedo_spheres); // Sphère derrière la boule
    Sphere sFront(Vector(0, 0, 1000), 940, albedo_spheres); // Sphère derrière la caméra
    Sphere sUp(Vector(0, 1000, 0), 940, albedo_spheres);    // Plafond
    Sphere sDown(Vector(0, -1000, 0), 990, albedo_spheres); // Sol
    Sphere sRight(Vector(1000, 0, 0), 940, albedo_spheres); // Mur droit
    Sphere sLeft(Vector(-1000, 0, 0), 940, albedo_spheres); // Mur gauche

    Sphere sLum(L, 5, Vector(1., 1., 1.), false, false, true); // Sphere de lumiere
    scene.add(&sLum);
    scene.Light = &sLum;

    // scene.add(&s1);
    // scene.add(&s2);
    // scene.add(&s3);

    scene.add(&tri);

    scene.add(&sFront);
    scene.add(&sBack);
    scene.add(&sUp);
    scene.add(&sDown);
    scene.add(&sRight);
    scene.add(&sLeft);

    progressbar bar(H);

    auto start = std::chrono::high_resolution_clock::now();
    std::vector<unsigned char> image(W * H * 3, 0);          // Crée un tableau 1D de W*H*3 éléments initialisés à 0 (l'image)
#pragma omp parallel for schedule(dynamic, 1) num_threads(8) // Parallélisation du calcul, mettre un flag openmp à gcc. Ici on fait sur toutes les lignes de l'image
    for (int i = 0; i < H; i++)
    {
#pragma omp critical
        bar.update();
        for (int j = 0; j < W; j++)
        {
            Vector intensity;
            for (int k = 0; k < nb_rays_monte_carlo; k++)
            {
                Vector dPixel;
                if (ANTIALIASING)
                    dPixel = randh.box_muller(0.3);
                Vector u(j - W / 2 + 0.5 + dPixel[0], (H - i) - (H / 2) + 0.5 + dPixel[1], -W / (2 * tanfov2));
                u.normalize();
                Ray r(C, u);

                if (DEPTH_OF_FIELD)
                {
                    // Profondeur de champ
                    Vector dAperture = randh.box_muller(1.);
                    Vector Cprime = C + dAperture;
                    Vector uprime = C + ddof / abs(u[2]) * u - Cprime;
                    uprime.normalize();
                    r = Ray(Cprime, uprime); // Rayon issu de C dans la direction u
                }

                intensity = intensity + scene.getColor(r, rebonds, true);
            }
            intensity = intensity / nb_rays_monte_carlo;

            image[(i * W + j) * 3 + 0] = std::min(255., std::pow(intensity[0], 1 / 2.2)); // R - La puissance 1/2.2 correspond à la correction gamma
            image[(i * W + j) * 3 + 1] = std::min(255., std::pow(intensity[1], 1 / 2.2)); // G
            image[(i * W + j) * 3 + 2] = std::min(255., std::pow(intensity[2], 1 / 2.2)); // B
        }
    }

    stbi_write_png("image.png", W, H, 3, &image[0], 0);
    auto end = std::chrono::high_resolution_clock::now();
    auto diff = end - start;
    auto diff_sec = std::chrono::duration_cast<std::chrono::milliseconds>(diff);
    std::cout << std::endl
              << "Run time : " << diff_sec.count() << "ms (" << diff_sec.count() / 1000. << "s)" << std::endl;

    return 0;
}
