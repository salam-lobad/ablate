---
layout: default
title: Mesh Generation
nav_order: 1
parent: Simulation Guides
grand_parent: Running Simulations
has_children: false
---

This page will include guides for different methods of creating meshes for running simulations.

## Creating 2d Meshes Using Gmsh

The following examples will follow the creation of a 2d mesh of a combustion chamber in Gmsh. 

### 1) Setting up a Geometry in Gmsh

In order to generate a mesh, having a geometry in Gmsh is first required. This can be done with the following two approaches, either defining the combustion chamber geometry in Gmsh or importing the geometry from a CAD file.

#### **1.1 Creating 2d Geometries in Gmsh**

1. Open the Gmsh application
2. Create a new .geo file
3. Add points around perimeter of combustion chmaber boundary
   1. Select add points (Modules -> Geometry -> Add -> Point)
   2. Add coordinates for points and press "e" to add points
   3. Add points in counterclockwise direction around perimeter to avoid inverted cells
   4. Press "q" to abort after adding all cells
4. Add lines connecting points
   1. Select add lines (Modules -> Geometry -> Add -> Line) 
   2. Select starting points and ending points
   3. Where applicable add splines (Modules -> Geometry -> Add -> Spline)
   4. Select control points
   5. Good practice is to connect points with lines in clockwise order to avoid inverted cells
   6. Press "q" to abort after add all lines

#### **1.2 Importing 2d Slices of 3d CAD Geometries**

A 2d mesh of a slice of a 3d cad geometry can be made using a CAD model in solidworks. In solidworks, looking at a 3d CAD file of your desired geometry, one can place a plane intersecting the part wherever they want their 2d mesh. 

The following steps are only necessary if one has a CAD model/assembly of a combustion chamber with a hollow flow region. These steps can be used to fill the empty flow region with a new part from which the mesh will later be generated from. In solidworks a mold cavity can be filled using the Combine feature.

1. Open a solidworks assembly with a combustion chamber model. 
2. Insert a new part under (Insert Components -> New Part)
   1. Select a refrence plane to sketch on to create a new part.
   2. In that part create a sketch that can be extruded or revolved to fill the combustion chamber casing and exit sketch.
3. Use the join feature to create on part out of all the combustion chamber. (Insert > Features > Join) Group select all of the parts of the assembly.
4. Extrude/revolve the earlier sketch as is applicable making sure to keep "Merge Results" unchecked.
5. Use the combine feature (Insert > Features > Combine)
   1. Check Subtract.
   2. Choose the Boss-Extrude/Revolve as the main body.
   3. Combine with the Join Body.
6. Save the part that has been created for the chamber's flow region.

Using the following steps create .step file containing the 2d geometry to be imported into Gmsh. 

1. Place a plane intersecting with your model showing your desired 2d surface, or if possible use of the three default planes in solidworks (Front, Top, Right.)
2. Use the slicing Tool to create a 2d sketch of your desired surface
   1. Select the slicing tool under (Insert -> Slicing) 
   2. .Under "Slicing Tools" select the desired plane
   3. Set the number of planes to create as one.
3. Save created sketch as a block under (Tools -> Blocks -> Save)
4. Create a step file of the 2d geometry to import into Gmsh
   1. Select Planar Surface under (Insert -> Surface -> Planar)
   2. Select the sketch as the bounding entity and create the surface.
   3. Save file as a .step file.

Import your .step file in Gmsh

1. Open the Gmsh application
2. Create a new .geo file in the same directory as your .step file
   1. If prompted select OpenCascade as the kernel to use
3. Merge your geometry into the .geo file
   1. Edit .geo script under (Modules -> Geometry -> Add -> Edit Script)
   2. In the .geo file add the line [ Merge "file_name_here.stp"; //+ ]
   3. Save the .geo script and reload in Gmsh (Modules -> Geometry -> Add -> Reload Script)

### 2) Setting up a 2d Mesh in Gmsh

The following are steps create a mesh in Gmsh from a 2d geometry. There are two methods here, one is to create an unstrctured mesh with traingular cells, and another is the create a structured mesh with quadrangular cells.

#### **2.1 Creating an Unstructured Triangular Mesh**

Text

#### **2.2 Creating an Structured Quadrangular Mesh**

Text

## Creating 3d Meshes Using Gmsh

The following examples will follow the creation of a 2d mesh of a combustion chamber in Gmsh. 

### 1) Setting up a Geometry in Gmsh

In order to generate a mesh, having a geometry in Gmsh is first required. This can be done with the following two approaches, either defining the combustion chamber geometry in Gmsh or importing the geometry from a CAD file.

#### **1.1 Creating 3d Geometries in Gmsh**

1. Open the Gmsh application
2. Create a new .geo file
3. Text
4. Text

#### **1.2 Importing 3d CAD Geometries**

The following are step to import a 2d CAD geometry into Gmsh.

The following steps are only necessary if one has a CAD model/assembly of a combustion chamber with a hollow flow region. These steps can be used to fill the empty flow region with a new part from which the mesh will later be generated from. In solidworks a mold cavity can be filled using the Combine feature.

1. Open a solidworks assembly with a combustion chamber model. 
2. Insert a new part under (Insert Components -> New Part)
   1. Select a refrence plane to sketch on to create a new part.
   2. In that part create a sketch that can be extruded or revolved to fill the combustion chamber casing and exit sketch.
3. Use the join feature to create on part out of all the combustion chamber. (Insert > Features > Join) Group select all of the parts of the assembly.
4. Extrude/revolve the earlier sketch as is applicable making sure to keep "Merge Results" unchecked.
5. Use the combine feature (Insert > Features > Combine)
   1. Check Subtract.
   2. Choose the Boss-Extrude/Revolve as the main body.
   3. Combine with the Join Body.
6. Save the part that has been created for the chamber's flow region.

Only a .step file can be imported into Gmsh. The following are instructions to save a CAD file in solidworks as a .step file.

1. In solidworks select (File -> Save as)
2. Under Save as type select "STEP AP214"
3. At the end of your file name add .stp if one wants to specify instead of .step
4. The following steps are optional in the event that a user wants to change the origin for the .step file as opposed to the origin in the solidworks part
   1. Make sure you've added a refrence origin in your solidworks part
   2. In the "save as" pop up select the "Options button"
   3. Under "Output Coordinate System" select your desired coordinate system
5. Save the .step file

A .step file of a 3d Cad geometry can be imported into Gmsh as follows

1. Open the Gmsh application
2. Create a new .geo file in the same directory as your .step file
   1. If prompted select OpenCascade as the kernel to use
3. Merge your geometry into the .geo file
   1. Edit .geo script under (Modules -> Geometry -> Add -> Edit Script)
   2. In the .geo file add the line [ Merge "file_name_here.stp"; //+ ]
   3. Save the .geo script and reload in Gmsh (Modules -> Geometry -> Add -> Reload Script)

### 2) Setting up a 3d Mesh in Gmsh

Text