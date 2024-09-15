# mod_zpe.c

mod_zpe is an Apache module for the ZPE Programming Environment and the YASS language. Use this to add support for the YASS language into the Apache Web Server. 

## Usage

<ul>
  <li>
    Copy all files as listed into a folder on your computer
  </li>
  <li>
    Make sure you have the latest version of ZPE from jamiebalfour.scot.
  </li>
  <li>
    Copy zpe.jar into a folder in the root called <code>/zpe/</code>
  </li>
  <li>
    Make sure make.sh is executable (<code>chmod 755 make.sh</code>)
  </li>
  <li>
    Run the make.sh file with <code>./make.sh</code>
  </li>
  <li>
    Copy the resulting mod_zpe.so file into the appropriate directory (<code>cp mod_zpe.so /usr/lib/apache2/modules/</code>)
  </li>
  <li>
    Restart Apache (<code>httpd -k restart</code> or <code>apachectl -k restart</code>)
  </li>
  <li>
    Add the mod_zpe.load file into the /etc/apache2/mods-available folder with the following line:<br>
    <code>LoadModule zpe_module /usr/lib/apache2/modules/mod_zpe.so</code>
  </li>
  <li>
    Restart Apache (<code>httpd -k restart</code> or <code>apachectl -k restart</code>)
  </li>
</ul>

## index.ywp

YASS Web Pages or YWP files work by embedding YASS code within <code>&lt;?ywp</code> and <code>ywp?&gt;</code>

<pre>

  &lt;!DOCTYPE html&gt;
  &lt;html&gt;
    &lt;head&gt;
    &lt;/head&gt;
    &lt;body&gt;
      &lt;?ywp
        print("Hello world!")
      ywp?&gt;
    &lt;/body&gt;    
  &lt;/html&gt;
  
</pre>
